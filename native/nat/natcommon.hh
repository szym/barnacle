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

#ifndef INCLUDED_NATCOMMON_HH
#define INCLUDED_NATCOMMON_HH

#include <stdio.h>
#include <netinet/ip.h> // <linux/ip.h>
#include <netinet/tcp.h> // can't use linux/tcp.h in userspace
#include <netinet/udp.h> // <linux/udp.h>
#include <linux/icmp.h> // no <net> option
#include <arpa/inet.h>
#include <asm/byteorder.h>

#undef NDEBUG
#include <assert.h>

#include "hashmap.hh"
#include "buffer.hh"
#include "socket.hh" // for PlugSocket
#include "log.hh"

static inline const void *transport_header(const Buffer& b) {
  return b.data() + (((const iphdr *)b.data())->ihl << 2);
}
static inline void *transport_header(Buffer& b) {
  return b.data() + (((const iphdr *)b.data())->ihl << 2);
}

// NOTE: this is very primitive header based on http://tools.ietf.org/html/rfc2637#section-4.1
struct grehdr {
   __u8  ignored;
#ifdef __LITTLE_ENDIAN_BITFIELD
   __u8  version:3,
         flags:5;
#elif defined(__BIG_ENDIAN_BITFIELD)
   __u8  flags:5,
         version:3;
#else
#error "Adjust your <asm/byteorder.h> defines"
#endif
   __u16 protocol,
         payload_len,
         call_id;        /* peer's call_id for this session */
};

const int GRE_VERSION_PPTP = 1;

struct IPFlowId {
  in_addr_t saddr; // all in network order
  in_addr_t daddr;
  uint16_t  sport; // (or icmp.echo.id)
  uint16_t  dport;
  uint8_t   protocol; // TODO: consider changing to 32bit for alignment

  IPFlowId() {} // only used for HashTable::_default

  IPFlowId(in_addr_t sa, in_addr_t da, uint16_t sp, uint16_t dp, uint8_t p)
    : saddr(sa), daddr(da), sport(sp), dport(dp), protocol(p) {}

  IPFlowId reverse() const {
    return IPFlowId(daddr, saddr, dport, sport, protocol);
  }

  /// extract flowid from the packet
  IPFlowId(const Buffer &b) : sport(0) {
    // FIXME: check sanity! (check if packet is long enough)
    const iphdr *ip = (const iphdr *)b.data();
    saddr = ip->saddr;
    daddr = ip->daddr;
    protocol = ip->protocol;
    switch (protocol) {
    case IPPROTO_ICMP: {
      const icmphdr *icmp = (const icmphdr *)transport_header(b);
      if ((icmp->type == ICMP_ECHO) || (icmp->type == ICMP_ECHOREPLY)) {
        sport = icmp->un.echo.id;
        dport = icmp->un.echo.id; // so that the echoreply matches too
      } // else we will ignore it
      break;
    } case IPPROTO_TCP: {
      const tcphdr *tcp = (const tcphdr *)transport_header(b);
      sport = tcp->source;
      dport = tcp->dest;
      break;
    } case IPPROTO_UDP: {
      const udphdr *udp = (const udphdr *)transport_header(b);
      sport = udp->source;
      dport = udp->dest;
      break;
    } case IPPROTO_GRE: {
      const grehdr *gre = (const grehdr *)transport_header(b);
      if (gre->version == GRE_VERSION_PPTP) // only support PPTP with call_id
        sport = dport = gre->call_id;
      break;
    } default: ;// leave sport == 0
    }
  }

  bool valid() const { return sport != 0; }

  /// hashcode from click
  inline hashcode_t hashcode() const {
#define ROT(v, r) ((v)<<(r) | ((unsigned)(v))>>(32-(r)))
    // more complicated hashcode, but causes less collision
    uint16_t s = ntohs(sport);
    uint16_t d = ntohs(dport);
    hashcode_t sx = ::hashcode(saddr);
    hashcode_t dx = ::hashcode(daddr);
    return (ROT(sx, s%16) ^ ROT(dx, 31-d%16)) ^ ((d << 16) | s) ^ protocol;
  }

  bool operator==(const IPFlowId &o) const {
    return (saddr == o.saddr) && (daddr == o.daddr) &&
           (sport == o.sport) && (dport == o.dport) && (protocol == o.protocol);
  }
  bool operator!=(const IPFlowId &other) const { return !(*this == other); }

  int unparse(char *s) const {
    if (s) {
      const uint8_t *p = (const uint8_t *)&saddr;
      const uint8_t *q = (const uint8_t *)&daddr;
      return sprintf(s, "(%d| %d.%d.%d.%d:%hu > %d.%d.%d.%d:%hu)", protocol,
                p[0], p[1], p[2], p[3], ntohs(sport),
                q[0], q[1], q[2], q[3], ntohs(dport));
    }
    return 0;
  }
}; // 13 bytes

static const char* unparse(const IPFlowId &id) {
  static char buf[64]; // 64 bytes is enough for any IPFlowId
  id.unparse(buf);
  return buf;
}

static inline void
update_in_cksum(uint16_t &csum, uint16_t delta) {
  uint32_t sum = (~csum & 0xFFFF) + delta;
  sum = (sum & 0xFFFF) + (sum >> 16);
  csum = ~(sum + (sum >> 16));
}

static inline void
make_icmp(Buffer &b, in_addr_t src, const icmphdr *hdr) {
  const size_t IcmpDataSize = sizeof(iphdr) + 8; // Data = old IP + 8 bytes
  size_t size = sizeof(iphdr) + sizeof(icmphdr) + IcmpDataSize;
  b.trim(0); b.put(size);
  iphdr *old_ip = (iphdr *)(b.data() + sizeof(iphdr) + sizeof(icmphdr));
  iphdr *ip = (iphdr *)b.data();
  memcpy(old_ip, ip, IcmpDataSize);
  memset(ip, sizeof(iphdr), 0);
  ip->saddr = src;
  ip->daddr = old_ip->saddr;
  ip->version = IPVERSION;
  ip->ttl = IPDEFTTL;
  ip->ihl = sizeof(iphdr) >> 2;
  ip->tot_len = htons(size);
  ip->protocol = IPPROTO_ICMP;
  ip->check = 0; // NOTE: IPSocket will update this anyway
  icmphdr *icmp = (icmphdr *)(b.data() + sizeof(iphdr));
  memcpy(icmp, hdr, sizeof(icmphdr));
  icmp->checksum = 0;
  const uint16_t *words = (const uint16_t *)icmp;
  unsigned check = 0;
  const int IcmpWords = (sizeof(icmphdr) + IcmpDataSize)/sizeof(uint16_t);
  for (int i = 0; i < IcmpWords; ++i) {
    check += words[i];
  }
  while (check & ~0xFFFF)
    check = (check & 0xFFFF) + (check >> 16);
  icmp->checksum = 0xFFFF ^ check;
}

static inline void
make_icmp_mtu(Buffer &b, in_addr_t src, int mtu) {
  icmphdr hdr;
  hdr.type = ICMP_DEST_UNREACH;
  hdr.code = ICMP_FRAG_NEEDED;
  hdr.un.frag.mtu = htons(mtu);
  make_icmp(b, src, &hdr);
}

/**
 * Take an IP packet and translate the addresses and ports in it
 */
class Translation {
protected:
  IPFlowId _mapto;
  uint16_t _ip_csum_delta;
  uint16_t _udp_csum_delta;
  // 17 bytes for a mapping
public:
  Translation(const IPFlowId &mapfrom, const IPFlowId &mapto) : _mapto(mapto) {
    // set checksum deltas (see click:iprw.cc)
    const uint16_t *source_words = (const uint16_t *)&mapfrom;
    const uint16_t *dest_words = (const uint16_t *)&mapto;
    unsigned delta = 0;
    for (int i = 0; i < 4; i++) {
      delta += ~source_words[i] & 0xFFFF;
      delta += dest_words[i];
    }
    delta = (delta & 0xFFFF) + (delta >> 16);
    _ip_csum_delta = delta + (delta >> 16);

    for (int i = 4; i < 6; i++) {
      delta += ~source_words[i] & 0xFFFF;
      delta += dest_words[i];
    }
    delta = (delta & 0xFFFF) + (delta >> 16);
    _udp_csum_delta = delta + (delta >> 16);
  }

  const IPFlowId &flowid() const { return _mapto; }

  /// set flowid on the packet, and update the checksum!
  void apply(Buffer &b) {
    iphdr *ip = (iphdr *)b.data();
    ip->saddr = _mapto.saddr;
    ip->daddr = _mapto.daddr;
    update_in_cksum(ip->check, _ip_csum_delta); // this is unnecessary for IPSocket

    // if not first fragment, there's no transport header
    if ((ip->frag_off & htons(0x1FFF)) != 0)
      return;

    // UDP/TCP header
    switch(_mapto.protocol) {
    case IPPROTO_ICMP: {
      // NOTE: we don't rewrite the echo id, so no need to update the checksum
      break;
    } case IPPROTO_TCP: {
      tcphdr *tcp = (tcphdr *)transport_header(b);
      tcp->source = _mapto.sport;
      tcp->dest = _mapto.dport;
      update_in_cksum(tcp->check, _udp_csum_delta);
      break;
    } case IPPROTO_UDP: {
      udphdr *udp = (udphdr *)transport_header(b);
      udp->source = _mapto.sport;
      udp->dest = _mapto.dport;
      if (udp->check)       // 0 checksum is no checksum
        update_in_cksum(udp->check, _udp_csum_delta);
      break;
    } case IPPROTO_GRE: {
      // do nothing
      break;
    } default:
      assert(0); // should never happen
    }
  }
};

/**
 * available ports
 */
class PortQueue {
  Queue<uint16_t> _q;
  PlugSocket *_plugs;
public:
  /// first == first port to try in host order
  PortQueue(unsigned numports, uint16_t first, bool tcp)
      : _q(numports) {
    // fill up the queue with ports (in sequence with some holes)
    _plugs = new PlugSocket[numports];
    for (uint16_t i = 0; i < numports; ++i) {
      // FIXME: what if we run out of ports!??
      while(!_plugs[i].plug(htons(first + i), tcp))
        ++first;
      _q.tail() = htons(first + i);
      _q.pushTail();
    }
  }
  ~PortQueue() {
    if (_plugs) {
      for (unsigned i = 0; i < _q.maxsize(); ++i)
        _plugs[i].close();
    }
    _plugs = NULL;
  }

  uint16_t alloc() {
    if (_q.empty()) return 0; // no port available
    uint16_t port = _q.head(); _q.popHead();
    return port;
  }
  void free(uint16_t port) {
    assert (!_q.full());
    _q.tail() = port; _q.pushTail();
  }
};

/**
 * map of preserved ports
 */
class PortMap {
public:
  typedef HashMap<uint16_t, bool> table_t;
private:
  table_t _table;
  PlugSocket *_plugs;
  unsigned _numplugs;
public:
  PortMap(unsigned numports, uint16_t ports[], bool tcp) : _numplugs(numports) {
    _plugs = new PlugSocket[_numplugs];
    for (unsigned i = 0; i < _numplugs; ++i) {
      uint16_t port = htons(ports[i]);
      if (!_plugs[i].plug(port, tcp)) {
        _plugs[i].close();
        DBG("Port %d cannot be preserved\n", ntohs(port));
        continue;
      }
      _table[port] = true;
      DBG("Preserved port %d\n", ntohs(port));
    }
  }
  ~PortMap() {
    if (_plugs) {
      for (unsigned i = 0; i < _numplugs; ++i)
        _plugs[i].close();
    }
    _plugs = NULL;
  }

  uint16_t alloc(uint16_t port) {
    table_t::iterator i = _table.find(port);
    if (i.live() && i->value) {
      i->value = false;
      return port;
    }
    return 0; // no port available
  }
  bool free(uint16_t port) {
    table_t::iterator i = _table.find(port);
    if (i.live()) {
      i->value = true;
      return true;
    }
    return false;
  }
};

/**
 * pool of all ports for rewriter
 */
class PortPool {
  PortMap _map; // preserved ports
  PortQueue _queue; // any ports
public:
  PortPool(unsigned numpreserved, uint16_t preserved[],
           unsigned numqueued, uint16_t firstqueued,
           bool plug) :
     _map(numpreserved, preserved, plug),
     _queue(numqueued, firstqueued, plug) {}

  uint16_t alloc(uint16_t port) {
    if (_map.alloc(port)) {
      return port;
    }
    return _queue.alloc();
  }
  void free(uint16_t port) {
    if (!_map.free(port))
      _queue.free(port);
  }
};

template <typename Mapping>
class RewriterStub {
public:
  struct Config {
    in_addr_t out_addr;
    in_addr_t netmask;
    in_addr_t subnet;
    unsigned  numpreserved;
    uint16_t  *preserved;
    unsigned  numports;
    uint16_t  firstport;
    bool      log;
  };
protected:
  Config _cfg;
  typedef HashMap<typename Mapping::IdOut, Mapping*> mapout_t;
  typedef HashMap<typename Mapping::IdIn,  Mapping*> mapin_t;
  mapout_t _out; // outgoing
  mapin_t _in; // incoming

  PortPool _uports; // available UDP ports
  PortPool _tports; // available TCP ports

  void remove(Mapping *m) {
    typename mapout_t::iterator it = _out.find(m->out());
    remove(it);
  }

  void remove(typename mapout_t::iterator &it) { // it is in _out
    Mapping *m = it->value; assert(it.live());
    it = _out.erase(it);
    size_t ner = _in.erase(m->in());
    assert(ner == 1);
    assert(_out.size() == _in.size());
    uint16_t port = m->port();
    switch (m->protocol()) {
    case IPPROTO_UDP:
      _uports.free(port);
      break;
    case IPPROTO_TCP:
      _tports.free(port);
      break;
    default:
      // else no port to free
      break;
    }
    if (_cfg.log) DBG("DEL %s ==> %d\n", unparse(m->out()), ntohs(port));
    delete m;
  }

  Mapping* map(const IPFlowId &out, uint16_t port) {
    Mapping *m = new Mapping(out, _cfg.out_addr, port);
    _in[m->in()] = m;
    _out[m->out()] = m;
    assert(_out.size() == _in.size());
    if (_cfg.log) DBG("NEW %s ==> %d\n", unparse(out), ntohs(m->port()));
    return m;
  }

  void freePort(uint16_t port) { // FIXME: this is highly inefficient
    for (typename mapout_t::iterator it = _out.begin(); it.live(); ) {
      Mapping *m = it->value;
      uint8_t proto = m->protocol();
      if ((m->port() == port) &&
          ((proto == IPPROTO_TCP) || (proto == IPPROTO_UDP)))
        remove(it);
      else
        ++it;
    }
  }

  bool filtered(const IPFlowId &id) { // ignore broadcast and LAN packets
    return ((id.daddr == (in_addr_t)-1)
        || ((id.daddr & _cfg.netmask) == _cfg.subnet));
  }

public:
  RewriterStub(const Config &c):
    _cfg(c),
    _uports(c.numpreserved, c.preserved, c.numports, c.firstport, false),
    _tports(c.numpreserved, c.preserved, c.numports, c.firstport, true) {}

  void configure(const Config &c) {
    _cfg = c;
  }

#ifdef NAT_OPEN
  void setDmz(in_addr_t dmz) {
    DBG("DMZ for %d ports\n", _cfg.numpreserved);
    int succeeded = 0;
    for (unsigned i = 0; i < _cfg.numpreserved; ++i) {
      uint16_t port = htons(_cfg.preserved[i]);
      freePort(port);
      uint16_t nport = _uports.alloc(port);
      if (nport == port) {
        map(IPFlowId(dmz, 0, port, port, IPPROTO_UDP), port);
        ++succeeded;
      } else {
        _uports.free(nport);
      }
      nport = _tports.alloc(port);
      if (nport == port) {
        map(IPFlowId(dmz, 0, port, port, IPPROTO_TCP), port);
        ++succeeded;
      } else {
        _tports.free(nport);
      }
    }
    // port forward GRE 47
    uint16_t port = htons(47);
    map(IPFlowId(dmz, 0, port, port, IPPROTO_GRE), port);
    DBG("DMZ configured for %d ports\n", succeeded);
  }
#endif

  /// handle packet going in -> out
  bool packetOut(Buffer &b) {
    IPFlowId out(b);
    if (!out.valid()) return false; // unrecognized protocol

    Mapping *m = _out.get(out);
    assert(_out.size() == _in.size());
    if (!m) {
      if (filtered(out)) return false;

      uint16_t port = out.sport;
      switch (out.protocol) {
      case IPPROTO_UDP:
        port = _uports.alloc(port);
        if (port == 0) {
          DBG("OUT OF UDP PORTS!\n");
          return false;
        }
        break;
      case IPPROTO_TCP:
        port = _tports.alloc(port);
        if (port == 0) {
          DBG("OUT OF TCP PORTS!\n");
          return false;
        }
        break;
      default:
        // else leave the echo.id untouched
        break;
      }
      m = map(out, port);
    }
    m->applyOut(out, b);
    if (m->done()) remove(m);
    return true;
  }

  /// handle packet going out -> in
  bool packetIn(Buffer &b) {
    IPFlowId in(b);
    if (!in.valid()) return false;
    Mapping *m = _in.get(in);
    assert(_out.size() == _in.size());
    if (!m) return false; // unrelated flow, firewalled
    m->applyIn(in, b);
    if (m->done()) remove(m);
    return true;
  }

  /// clean up long unused mappings
  void cleanup(bool keep_tcp) {
    for (typename mapout_t::iterator it = _out.begin(); it.live(); ) {
      Mapping *m = it->value;
      if (m->used() || (keep_tcp && (m->protocol() == IPPROTO_TCP))) {
        // NOTE: if we still have a TCP mapping, it's not done yet
        m->reset();
        ++it;
      } else remove(it);
    }
  }
  int size() const { return _in.size(); }
};


#endif // INCLUDED_NATCOMMON_HH

