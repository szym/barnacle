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

#ifndef INCLUDED_DHCP_HH
#define INCLUDED_DHCP_HH

#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <net/if.h> // for IFNAMSIZ
#include <time.h>
#include <assert.h>


/* see RFC 2131 and 2132 for DHCP protocol description */
enum {
  BOOTREQUEST = 1, // to server
  BOOTREPLY   = 2, // to client
};

const uint32_t DHCP_MAGIC_COOKIE = 0x63825363; // in network order

enum {
  DHCPDISCOVER = 1, // client locates servers
  DHCPOFFER    = 2, // server offers ip and params
  DHCPREQUEST  = 3, // client requests IP
  DHCPDECLINE  = 4, // client claims the IP is in use
  DHCPACK      = 5, // server acks ip and params (committed)
  DHCPNAK      = 6, // server nacks: wrong IP or lease expired
  DHCPRELEASE  = 7, // client relinquishes IP / cancels lease
  DHCPINFORM   = 8, // client asks for local conf params (has IP)
};

// The only options we support:
enum {
  OP_PAD              = 0,
  OP_NETMASK          = 1,
  OP_ROUTER           = 3,
  OP_DNSSERVER        = 6,
  OP_HOSTNAME         = 12,
  // OP_DOMAINNAME       = 15,
  // OP_BROADCAST        = 28,
  OP_REQUESTED_IP     = 50,
  OP_LEASE_TIME       = 51,
  OP_MESSAGE_TYPE     = 53,
  OP_SERVER_IDENTIFIER= 54,
  // OP_CLIENTID         = 61,
  OP_END              = 255,
};


const int DHCP_CHADDR_MAX = 16;
const int DHCP_MIN_OPS = 64; // enough to hold _our_ options
struct dhcp_packet {
  uint8_t   op, htype, hlen, hops;
  uint32_t  xid;
  uint16_t  secs, flags;
  in_addr_t ciaddr, yiaddr, siaddr, giaddr;
  uint8_t   chaddr[DHCP_CHADDR_MAX];
  uint8_t   sname[64];
  uint8_t   file[128];
  uint32_t  cookie;
  uint8_t   options[DHCP_MIN_OPS];
};

const int DHCP_MIN_SIZE = sizeof(dhcp_packet) - DHCP_MIN_OPS;

class DHCP {
public:
  struct Config {
    // in network order:
    in_addr_t netmask;
    in_addr_t subnet; // the subnet part of gw and ip range
    in_addr_t gw; // = DHCP server and route gw
    in_addr_t dns1;
    in_addr_t dns2;
    // in host order:
    uint16_t  firsthost, numhosts; // ip range
    time_t    leasetime; // in seconds
    char      ifname[IFNAMSIZ]; // LAN interface to bind to
  };
protected:
  Config _cfg;

  int _sock; // synchronous (blocking) UDP
  dhcp_packet _req, _resp, _resp_info, _resp_nak; // prepared responses

  int _last_offer; // most trivial way of handling decline

  /// one lease per ip in the ip range, no durable storage
  struct Lease {
    uint8_t   chaddr[DHCP_CHADDR_MAX];
    unsigned  hlen; // actual length of chaddr
    time_t    expiry; // = 0 means it's free
    Lease() : hlen(0), expiry(0) {
      ::memset(chaddr, 0, DHCP_CHADDR_MAX);
    }
    bool ownedby(const uint8_t *cha) const {
      return ::memcmp(chaddr, cha, hlen) == 0;
    }
  }; // 24 bytes
  Lease *_leases;

  /// returns -1 if we don't have such addr
  int addr2idx(in_addr_t addr) const {
    if ((addr & _cfg.netmask) != _cfg.subnet)
      return -1; // wrong subnet
    unsigned host = ntohl(addr & ~_cfg.netmask);
    if (host >= _cfg.firsthost) {
      host -= _cfg.firsthost;
      if (host < _cfg.numhosts)
        return host;
    }
    return -1;
  }

  in_addr_t idx2addr(int idx) const {
    assert((idx >= 0) && (idx < _cfg.numhosts));
    return htonl(_cfg.firsthost + idx) | _cfg.subnet;
  }

  void parse_req(int &msg_type, in_addr_t &requested_ip, char* &hostname);
  dhcp_packet * handle_discover();
  dhcp_packet * handle_request(in_addr_t requested_ip, char* hostname);

public:
  DHCP(const Config &c);
  ~DHCP() { delete [] _leases; close(_sock); }

  /// setup the UDP socket
  bool init();

  /// return false on critical failure
  bool run();
};

#endif // INCLUDED_DHCP_HH

