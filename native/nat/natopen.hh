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

#ifndef INCLUDED_NAT_HH
#define INCLUDED_NAT_HH

#include "natcommon.hh"

// used to identify outgoing flows
struct IPFlowIdOut : public IPFlowId {
  IPFlowIdOut() {}
  IPFlowIdOut(const IPFlowId &id) : IPFlowId(id) {}
  inline hashcode_t hashcode() const {
    uint16_t s = ntohs(sport);
    hashcode_t sx = ::hashcode(saddr);
    return ROT(sx, s%16) ^ s ^ protocol;
  }
  bool operator==(const IPFlowId &o) const {
    return (saddr == o.saddr) &&
           (sport == o.sport) && (protocol == o.protocol);
  }
  bool operator!=(const IPFlowId &other) const { return !(*this == other); }
};
// used to identify incoming flows
struct IPFlowIdIn : public IPFlowId {
  IPFlowIdIn() {}
  IPFlowIdIn(const IPFlowId &id) : IPFlowId(id) {}
  inline hashcode_t hashcode() const {
    uint16_t d = ntohs(dport);
    hashcode_t dx = ::hashcode(daddr);
    return ROT(dx, d%16) ^ d ^ protocol;
  }
  bool operator==(const IPFlowId &o) const {
    return (daddr == o.daddr) &&
           (dport == o.dport) && (protocol == o.protocol);
  }
  bool operator!=(const IPFlowId &other) const { return !(*this == other); }
};

/**
 * Mapping in full cone remembers source address/port only
 */
class MappingFullCone {
  IPFlowId _id; // this is only for convenience really...
  // .saddr = internal source address
  // .daddr = external source address
  // .sport = internal source port
  // .dport = external source port

  bool _used; // dirty flag
  enum {
    F_CLEAR = 0, F_OUT_DONE = 1, F_IN_DONE = 2, F_DONE = 3
  };
  int _flags;
public:

  typedef IPFlowIdOut IdOut;
  typedef IPFlowIdIn IdIn;

  // NOTE: only source address/port is stored
  MappingFullCone(const IPFlowId &before, in_addr_t newsrc, uint16_t newport)
    : _id(IPFlowId(before.saddr, newsrc, before.sport, newport, before.protocol)),
      _flags(F_CLEAR) {}

  /// this is what we use for hash keys
  IdOut out() const { return _id; } // only src matters
  IdIn  in()  const { return _id; } // only dst matters
  uint16_t protocol()      const { return _id.protocol; }

  /// look out for SYN, FIN and RST packets
  void updateFlags(const Buffer &b, bool out) {
    if (protocol() != IPPROTO_TCP) return;
    const tcphdr *tcp = (const tcphdr *)transport_header(b);
    if (tcp->rst)      { _flags &= out ? ~F_OUT_DONE : ~F_IN_DONE; }
    else if (tcp->fin) { _flags |= out ? F_OUT_DONE : F_IN_DONE; }
    //if (tcp->fin) { _flags = F_DONE; }
    else if (tcp->syn) { _flags = F_CLEAR; }
  }

  void applyOut(const IPFlowId &before, Buffer &b) {
    IPFlowId after(_id.daddr, before.daddr, _id.dport, before.dport, before.protocol);
    Translation(before, after).apply(b); // FIXME: this unnecessarily considers dst addr/port
    assert(IdIn(IPFlowId(b).reverse()) == in()); // TOO MANY TIMES THIS FAILS!
    updateFlags(b, true);
    _used = true;
  }

  void applyIn(const IPFlowId &before, Buffer &b) {
    IPFlowId after(before.saddr, _id.saddr, before.sport, _id.sport, before.protocol);
    Translation(before, after).apply(b);
    updateFlags(b, false);
    _used = true;
  }

  bool done() const { return (_flags == F_DONE); }
  bool used() const { return _used; }
  void reset() { _used = false; }
  uint16_t port() const {
    return _id.dport;
  }
};

/**
 * full cone NAPT
 */
typedef RewriterStub<MappingFullCone> Rewriter;

#endif // INCLUDED_NAT_HH

