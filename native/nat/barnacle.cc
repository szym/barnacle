/*
 *  This file is part of Barnacle Wifi Tether
 *  Copyright (C) 2010 by Szymon Jakubczak
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define TAG "NAT: "
#include "barnacle.hh"

Barnacle::~Barnacle() {
  if (have_ctrl()) {
    if (_ctrl_server.ok()) {
      unlink(_cfg.ctrl);
    }
    _ctrl_server.close();
    _ctrl.close();
  }

  _outs.close();
  _ins.close();
  _ips.close();
}

bool Barnacle::init_ctrl() {
  _nin = _nout = _bin = _bout = 0; // FIXME: remove
  _lastcleanup = _lastcleanup_tcp = 0;

  _ctrl.close();
  if (have_ctrl()) {
    if (!_ctrl_server.listen(_cfg.ctrl)) {
      ERR("Could not bind ctrl to %s: %s\n", _cfg.ctrl, strerror(errno));
      return false;
    }
    _sel.newFd(_ctrl_server.fd());
  } else {
    _ctrl_server.close();
  }

  return true;
}

bool Barnacle::start() {
  _sel.clear();
  _q.clear(); // is this necessary?

  _outs.close();
  _ins.close();
  _ips.close();

  _outs = PacketSocket(); // NOTE: this depends on not having destructors
  _ins.reset(); // NOTE: can't use constructor for the destructor will kill the hash
  _ips = IPSocket();

  if(_outs.fd() < 0 || !_outs.bind(_cfg.outif)) {
    ERR("Could not bind outif to %s : %s\n", _cfg.outif, strerror(errno));
    return false;
  }
  if(_ins.fd() < 0 || !_ins.bind(_cfg.inif)) {
    ERR("Could not bind inif to %s : %s\n", _cfg.inif, strerror(errno));
    return false;
  }
  if(_ips.fd() < 0 || !_ips.bind()) {
    ERR("Could not bind IP raw socket: %s\n", strerror(errno));
    return false;
  }

  _sel.newFd(_outs.fd());
  _sel.newFd(_ins.fd());
  _sel.newFd(_ips.fd());
  if (have_ctrl()) {
    _sel.newFd(_ctrl_server.fd());
  }

  IfCtl ic_in(_cfg.inif);
  IfCtl ic_out(_cfg.outif);
  // If this fails, we'll be sending "Fragmentation needed" when neccessary.
  ic_out.setMTU(1500);
  _mtu = ic_out.getMTU();

  // configure subnet, netmask and out_addr from interfaces
  _cfg.netmask   = ic_in.getMask();
  _cfg.subnet    = ic_in.getAddress() & _cfg.netmask;
  _cfg.out_addr  = ic_out.getAddress(); // if this is unset, somebody needs to set it

  _rw.configure(_cfg);

  if ((_cfg.out_addr == INADDR_NONE) || (_cfg.netmask == INADDR_NONE)) {
    // not good
    return false;
  }
  return true;
}


// NOTE: always return true (failure is non-critical)
void Barnacle::handle_ctrl() {
  if (_ctrl.ok()) {
    if (_sel.canRead(_ctrl.fd())) {
      if (_ctrl.recv(_msg) < 0) {
        _ctrl.close(); // we're done here
      } else if (_msg.is_complete()) {
        const char * b = _msg.msg();
        // messages we can handle now are:
        // MACA|<mac>
        // MACD|<mac>
        // FILT|<1|0>
        // DMZ|<ip>
        DBG("--- CONTROL --- %d : %s\n", _msg.msg_size(), b);
        if (_msg.msg_size() > 21 && !strncmp("MAC", b, 3)) {
          bool allowed = (b[3] == 'A');
          MACAddress mac;
          if (mac.read(b + 5)) {
            _ins.setFilter(mac, allowed);
            _ins.setFiltering(true); // for now we assume you want filtering
          } else DBG("Could not parse MAC %s\n", b + 4);
        } else if (_msg.msg_size() > 5 && !strncmp("FILT", b, 4)) {
          bool enabled = (b[5] == '1');
          _ins.setFiltering(enabled);
          DBG("Filtering %s\n", enabled ? "enabled" : "disabled");
        } else if (_msg.msg_size() > 10 && !strncmp("DMZ", b, 3)) {
#ifdef NAT_OPEN
          in_addr_t dmz = inet_addr(b + 4);
          if (dmz != INADDR_NONE)
            _rw.setDmz(dmz);
#endif
        }
        _msg.clear();
      }
    }
  } else if (_ctrl_server.ok()) {
    if (_sel.canRead(_ctrl_server.fd())) {
      _ctrl = _ctrl_server.accept();
      if (_ctrl.ok()) {
        _sel.newFd(_ctrl.fd());
      } else {
        // assume filtering failed
        // LOG("NAT filtering disabled\n");
        // _ins.setFiltering(false);
      }
    }
  }
}

// packets coming out -> in
bool Barnacle::handle_in() {
  if (_sel.canRead(_outs.fd())) {
    while(!_q.full()) {
      int l = _outs.recv(_q.tail());
      if (l == 0) {
        break;
      } else if (l > 0) {
        // packets out -> in
        Buffer &b = _q.tail();
        if (_rw.packetIn(b)) {
          _q.pushTail();
          _nin+= 1;
          _bin+= b.size(); // FIXME: remove
        }
      } else {
        return false;
      }
    }
  }
  return true;
}

bool Barnacle::handle_out() {
  if (_sel.canRead(_ins.fd())) {
    while(!_q.full()) {
      int l = _ins.recv(_q.tail());
      if (l == 0) {
        break;
      } else if (l > 0) {
        // packets in -> out
        Buffer &b = _q.tail();
        // check MTU
        if (b.size() > (unsigned)_mtu) {
          make_icmp_mtu(b, IfCtl(_cfg.inif).getAddress(), _mtu);
          _q.pushTail();
        } else if (_rw.packetOut(b)) {
          _q.pushTail();
          _nout+= 1;
          _bout+= b.size(); // FIXME: remove
        }
      } else {
        return false;
      }
    }
  }
  return true;
}

bool Barnacle::drain() {
  if (_sel.canWrite(_ips.fd())) {
    while(!_q.empty()) {
      int l = _ips.send(_q.head());
      if (l == 0) {
        break;
      } else if (l > 0) {
        _q.popHead();
      } else {
        if (errno == EMSGSIZE) {
          // un-applying the translation is tough, so we just adjust mtu
          int new_mtu = IfCtl(_cfg.outif).getMTU();
          if (new_mtu < _mtu) {
            _mtu = new_mtu;
            LOG("MTU adjusted to %d\n", _mtu);
          }
          _q.popHead();
        } else {
          // unhandled, need to restart
          return false;
        }
      }
    }
  }
  return true;
}

void Barnacle::cleanup() {
  // sporadically (every 5s) clean up obsolete mappings
  time_t now = time(0);
  if (now > _lastcleanup + _cfg.timeout) {
    bool keep_tcp = now < _lastcleanup_tcp + _cfg.timeout_tcp;
    _rw.cleanup(keep_tcp);
    _lastcleanup = now;
    if (!keep_tcp)
      _lastcleanup_tcp = now;
    DBG("--- Cleanup --- %d maps IN: %d %d OUT: %d %d\n",
        _rw.size(), _nin, _bin, _nout, _bout); // FIXME: remove
  }
}

// return false on I/O failure
bool Barnacle::run() {
  _sel.wantRead(_ins.fd(), !_q.full());
  _sel.wantRead(_outs.fd(), !_q.full());
  _sel.wantWrite(_ips.fd(), !_q.empty());

  if (_ctrl.ok()) {
    _sel.wantRead(_ctrl.fd(), true);
  } else if (_ctrl_server.ok()) {
    _sel.wantRead(_ctrl_server.fd(), true);
  }

  if ((_sel.select() <= 0) && (errno != EBADF)) {
    return false;
  }

  // update filter first
  if (have_ctrl())
    handle_ctrl();

  // LAN is faster, so first read packets from WAN
  if (!handle_in() ||
      !handle_out() ||
      !drain())
    return false;

  cleanup();
  return true;
}

