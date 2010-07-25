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

#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#include <log.hh>
#include "dhcp.hh"

DHCP::DHCP(const Config &c) : _cfg(c), _sock(0) {
  // for sanity
  _cfg.subnet = _cfg.subnet & _cfg.netmask;
  _cfg.gw     = _cfg.subnet | (_cfg.gw & ~_cfg.netmask);
  int maxhost = ~ntohl(_cfg.netmask);
  _cfg.firsthost &= maxhost;
  if(_cfg.firsthost + _cfg.numhosts > maxhost)
    _cfg.numhosts = maxhost - _cfg.firsthost;

  _leases = new Lease[_cfg.numhosts];

  // prepare response packets
  memset(&_resp, 0, sizeof(_resp)); // offer or request ack
  memset(&_resp_info, 0, sizeof(_resp_info)); // info ack
  memset(&_resp_nak, 0, sizeof(_resp_nak)); // nak

  _resp.op = _resp_info.op = _resp_nak.op = BOOTREPLY;
  _resp.cookie = _resp_info.cookie = _resp_nak.cookie = ntohl(DHCP_MAGIC_COOKIE);
#define SET_INT(idx, val) { ((uint32_t *)options)[idx] = (val); }
#define SET_MSG(pkt, val) { pkt->options[2] = val; }
  {
    // all this padding for alignment is probably overkill
    uint8_t options[] = {
      OP_MESSAGE_TYPE, 1, 0, OP_PAD,
      OP_PAD, OP_PAD, OP_NETMASK,   4, 0, 0, 0, 0,
      OP_PAD, OP_PAD, OP_ROUTER,    4, 0, 0, 0, 0,
      OP_PAD, OP_PAD, OP_DNSSERVER, 8, 0, 0, 0, 0, 0, 0, 0, 0,
      OP_PAD, OP_PAD, OP_SERVER_IDENTIFIER, 4, 0, 0, 0, 0,
      OP_PAD, OP_PAD, OP_LEASE_TIME, 4, 0, 0, 0, 0,
      OP_END
    };

    SET_INT(2, _cfg.netmask);
    SET_INT(4, _cfg.gw);
    SET_INT(6, _cfg.dns1);
    SET_INT(7, _cfg.dns2);
    if (!_cfg.dns2) options[6*4] = 4; // dns2 == PAD
    SET_INT(9, _cfg.gw);
    SET_INT(11, htonl(_cfg.leasetime));
    memcpy(_resp.options, options, sizeof(options));
    // no lease time on resp_info
    SET_INT(10, 0); SET_INT(11, 0);
    SET_MSG((&_resp_info), DHCPACK);
    memcpy(_resp_info.options, options, sizeof(options));
  }
  {
    uint8_t options[] = {
      OP_MESSAGE_TYPE, 1, DHCPNAK, OP_PAD,
      OP_PAD, OP_PAD, OP_SERVER_IDENTIFIER, 4, 0, 0, 0, 0,
      OP_END
    };
    SET_INT(2, _cfg.gw);
    memcpy(_resp_nak.options, options, sizeof(options));
  }
}

bool DHCP::init() {
  // sanity check
  if (!_cfg.numhosts) {
    ERR("numhosts = 0!\n");
    return false;
  }
  if (_sock) ::close(_sock);
  _sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (!_sock) {
    ERR("Could not create UDP socket: %s\n", strerror(errno));
    return false;
  }
  int one = 1;
  if(::setsockopt(_sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one))) {
    ERR("Could not set BROADCAST on socket: %s\n", strerror(errno));
    return false;
  }
  sockaddr_in sa;
  sa.sin_family = AF_INET;
  sa.sin_port = htons(67);
  sa.sin_addr.s_addr = INADDR_ANY; // to receive both broadcasts and unicast
  if(::bind(_sock, (sockaddr *)&sa, sizeof(sa))) {
    ERR("Could not bind to DHCP port: %s\n", strerror(errno));
    return false;
  }
  if(::setsockopt(_sock, SOL_SOCKET, SO_BINDTODEVICE, _cfg.ifname, IFNAMSIZ)) {
    ERR("Could not bind to %s: %s\n", _cfg.ifname, strerror(errno));
    return false;
  }
  _last_offer = 0;
  return true;
}

// make sure s is printable
static inline char * make_printable(char * s) {
  if (s) {
    for(char *p = s; *p; ++p)
      if (!isgraph(*p))
        *p = '#';
  }
  return s;
}

void DHCP::parse_req(int &msg_type, in_addr_t &requested_ip, char * &hostname) {
  // parse options to figure out message type and requested IP address
  const uint8_t *ops = _req.options;
  // check for DHCP cookie first
  if (_req.cookie == ntohl(DHCP_MAGIC_COOKIE)) {
    for (int i = 0; i < DHCP_MIN_OPS; ) {
      switch(ops[i]) {
      case OP_PAD:
        ++i;
        continue;
      case OP_END:
        i = DHCP_MIN_OPS;  // exit loop
        continue;
      case OP_MESSAGE_TYPE:
        msg_type = ops[i+2];
        break;
      case OP_REQUESTED_IP:
        //requested_ip = *(in_addr_t *)(ops+i+2);
        // to avoid sigbus, we use memcpy
        memcpy(&requested_ip, ops+i+2, sizeof(in_addr_t));
        break;
      case OP_HOSTNAME:
        hostname = (char *)(ops+i+2);
        break;
      default: break;
      }
      i+= ops[i+1] + 2;
    }
    if (hostname) { // safety measures
      int len = hostname[-1];
      if (hostname + len < (const char *)&_req + sizeof(_req))
        hostname[len] = '\0'; // this is ok, within _req
      else hostname = NULL; // to prevent buffer overrun
    }
  } else {
    DBG("DHCP no magic cookie\n");
    // BOOTP fallback
    requested_ip = _req.ciaddr ? _req.ciaddr : _req.yiaddr;
    msg_type = requested_ip ? DHCPREQUEST : DHCPDISCOVER;
  }
}

dhcp_packet * DHCP::handle_discover() {
  dhcp_packet * r = 0; // response
  // find a spare IP, and propose it (no Lease)
  time_t now = time(0);
  int idx = 0;
  for (idx = _last_offer; idx < _cfg.numhosts; ++idx) {
    if (_leases[idx].expiry < now) { // expired
      _leases[idx].expiry = 0;
      break;
    }
  }
  if (idx < _cfg.numhosts) { // found available address
    r = &_resp;
    r->yiaddr = idx2addr(idx);
    SET_MSG(r, DHCPOFFER);
    DBG(">>> DHCP offered %d\n", idx);
    ++_last_offer;
    if (_last_offer > _cfg.numhosts) _last_offer = 0;
  } else LOG("OUT OF IP ADDRESSES!\n");
  return r;
}

dhcp_packet * DHCP::handle_request(in_addr_t requested_ip, char *hostname) {
  dhcp_packet * r = 0; // response
  // check if requested IP available
  if (!requested_ip)
    requested_ip = _req.ciaddr; // renewal
  int idx = addr2idx(requested_ip);
  time_t now = time(0);
  if ((idx >= 0)
      && ((_leases[idx].expiry < now) || _leases[idx].ownedby(_req.chaddr))) {
    const uint8_t *p = (const uint8_t *)&requested_ip;
    DBG("DHCPACK %d.%d.%d.%d\n", p[0],p[1],p[2],p[3]);
    const uint8_t *e = (const uint8_t *)_req.chaddr;
    // NOTE: assume hlen = 6
    LOG("DHCPACK %02x:%02x:%02x:%02x:%02x:%02x %d.%d.%d.%d %s\n",
      e[0],e[1],e[2],e[3],e[4],e[5], p[0],p[1],p[2],p[3], hostname ? make_printable(hostname) : "");
    fflush(stdout);
    // setup the new lease
    _leases[idx].expiry = now + _cfg.leasetime;
    _leases[idx].hlen = _req.hlen < DHCP_CHADDR_MAX ? _req.hlen : DHCP_CHADDR_MAX;
    memcpy(_leases[idx].chaddr, _req.chaddr, DHCP_CHADDR_MAX);
    // ack it
    r = &_resp;
    r->yiaddr = requested_ip;
    SET_MSG(r, DHCPACK);
  } else {
    DBG("DHCNAK %d\n", idx);
    r = &_resp_nak;
  }
  return r;
}

bool DHCP::run() {
  // recv from socket, long packets are truncated
  ::memset(&_req, 0, sizeof(_req));
  int len = ::recv(_sock, &_req, sizeof(_req), MSG_TRUNC); // note, no timeout
  if (len < 0) {
    ERR("DHCP recv failed: %s\n", strerror(errno));
    return false;
  }
  if (len < DHCP_MIN_SIZE) {
    DBG("DHCP packet too short %d bytes < %d\n", len, DHCP_MIN_SIZE);
    return true; // ignore it
  }
  if (_req.op != BOOTREQUEST) {
    DBG("DHCP not a BOOTREQUEST: %d\n", _req.op);
    return true; // ignore it
  }

  int       msg_type      = -1;
  in_addr_t requested_ip  = 0;
  char      *hostname     = NULL;

  parse_req(msg_type, requested_ip, hostname);

  dhcp_packet *r = 0; // response

  switch(msg_type) {

  case DHCPRELEASE: { // client relinquishes IP / cancels lease
    int idx = addr2idx(_req.ciaddr);
    if (idx >= 0) {
      // check if it owns the address
      if (_leases[idx].ownedby(_req.chaddr)) {
        DBG("DHCPRELEASE %d\n", idx);
        _leases[idx].expiry = 0;
      }
    } else DBG("DHCPRELEASE INVALID\n");
  } break;

  case DHCPDISCOVER: { // client locates servers
    r = handle_discover();
  } break;

  case DHCPREQUEST: { // client requests IP
    r = handle_request(requested_ip, hostname);
  } break;

  case DHCPDECLINE: { // client claims the IP is in use
    DBG("DHCPDECLINE %d\n", requested_ip);
    int idx = addr2idx(requested_ip);
    if (idx >= 0) {
      _leases[idx].expiry = time(0) + _cfg.leasetime / 2;
    }
  } break;

  case DHCPINFORM: // client asks for local conf params
    r = &_resp_info;
    r->ciaddr = _req.ciaddr;
    break;

  default:
    DBG("DHCP unknown message type: %d\n", msg_type);
    break;

  }
  if (!r) return true;

  // the rest of the response
  r->htype  = _req.htype;
  r->hlen   = _req.hlen;
  r->xid    = _req.xid;
  r->flags  = htons(0x8000); // because we can't bypass ARP
  r->giaddr = _req.giaddr; // actually, this should be ignored
  ::memcpy(r->chaddr, _req.chaddr, DHCP_CHADDR_MAX);

  sockaddr_in sa;
  sa.sin_family = AF_INET;
  sa.sin_port   = htons(68);
  sa.sin_addr.s_addr = r->yiaddr;
  if ((r->yiaddr == 0) || (r->flags & htons(0x8000))) {
    sa.sin_addr.s_addr = (in_addr_t)0xFFFFFFFF; // broadcast it
  }
  len = ::sendto(_sock, r, sizeof(dhcp_packet), 0, (sockaddr *)&sa, sizeof(sa));
  if (len <= 0) {
    ERR("DHCP send failed: %s\n", strerror(errno));
    return false;
  }
  return true;
}

