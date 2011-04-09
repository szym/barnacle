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

/* Network I/O library for Barnacle: MAC filtering socket */
#ifndef INCLUDED_FILTERSOCKET_HH
#define INCLUDED_FILTERSOCKET_HH

#include "socket.hh"
#include "hashtable.hh"
#include "macaddress.hh"

class FilterSocket : public PacketSocket {
protected:
  typedef HashTable< HashAdapter<MACAddress> > hash_t;
  hash_t _hash;
  bool filtering;
public:
  FilterSocket(bool filt = false) : filtering(filt) {}

  void reset() {
    // FIXME: this is C&P from PacketSocket::PacketSocket()
    _fd = ::socket(AF_PACKET, SOCK_DGRAM | O_NONBLOCK, htons(ETHERTYPE_IP));
    _hash.clear();
  }

  void setFiltering(bool filt) { filtering = filt; }

  void setFilter(const MACAddress &addr, bool allowed) {
    const uint8_t *a = addr.addr;
    DBG("MAC filter %02x:%02x:%02x:%02x:%02x:%02x %s\n", a[0],a[1],a[2],a[3],a[4],a[5], allowed ? "allow" : "deny");
    if (allowed) {
      _hash.find_insert(addr);
      assert(_hash.find(addr).live());
    } else {
      _hash.erase(addr);
      assert(!_hash.find(addr).live());
    }
  }

  /// return 0 on try again, -1 on fail
  int recv(Buffer &b) {
    b.clear();
    sockaddr_ll sll;
    socklen_t slen = sizeof(sll);
    int len = ::recvfrom(_fd, b.data(), b.room(), MSG_TRUNC, (sockaddr *)&sll, &slen);
    if (len > 0) {
      if(sll.sll_pkttype != PACKET_OUTGOING) {
        if (filtering && !_hash.find(sll.sll_addr).live())
          return 0; // no packet
        b.put(len);
        return len;
      }
      return 0;
    }
    return ((len < 0) && (errno == EAGAIN)) ? 0 : -1;
  }
};

#endif // INCLUDED_FILTERSOCKET_HH

