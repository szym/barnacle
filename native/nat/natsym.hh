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

/**
 * Mapping is a pair of translations
 */
class MappingSymmetric {
  Translation _out; // for packets from in to out
  Translation _in; // for packets from out to in
  bool _used; // dirty flag
  enum {
    F_CLEAR = 0, F_OUT_DONE = 1, F_IN_DONE = 2, F_DONE = 3
  };
  int _flags;
public:

  typedef IPFlowId IdOut;
  typedef IPFlowId IdIn;

  MappingSymmetric(const IPFlowId &before, in_addr_t newsrc, uint16_t newport)
    : _out(before, IPFlowId(newsrc, before.daddr, newport, before.dport, before.protocol)),
      _in(_out.flowid().reverse(), before.reverse()), _flags(F_CLEAR) {}

  /// this is what we use for hash keys, used rarely
  IdOut out() const { return  _in.flowid().reverse(); }
  IdIn  in()  const { return _out.flowid().reverse(); }
  uint16_t protocol()   const { return _in.flowid().protocol; }

  /// look out for SYN, FIN and RST packets
  void updateFlags(const Buffer &b, bool out) {
    if (protocol() != IPPROTO_TCP) return;
    const tcphdr *tcp = (const tcphdr *)transport_header(b);
    if (tcp->rst)      { _flags &= out ? ~F_OUT_DONE : ~F_IN_DONE; }
    else if (tcp->fin) { _flags |= out ? F_OUT_DONE : F_IN_DONE; }
    //if (tcp->fin) { _flags = F_DONE; }
    else if (tcp->syn) { _flags = F_CLEAR; }
  }

  void applyOut(const IPFlowId &, Buffer &b) {
    _out.apply(b);
    updateFlags(b, true);
    _used = true;
  }

  void applyIn(const IPFlowId &, Buffer &b) {
    _in.apply(b);
    updateFlags(b, false);
    _used = true;
  }

  bool done() const { return (_flags == F_DONE); }
  bool used() const { return _used; }
  void reset() { _used = false; }
  uint16_t port() const {
    return _out.flowid().sport;
  }
};

/**
 * symmetric NAPT
 */
typedef RewriterStub<MappingSymmetric> Rewriter;

#endif // INCLUDED_NAT_HH

