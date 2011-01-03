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

#ifndef INCLUDED_IFCTL_HH
#define INCLUDED_IFCTL_HH

#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <linux/if.h> // for ifreq, to be compatible with linux/wireless.h

#include "log.hh"

static inline bool ether_parse(const char *inp, uint8_t *a) {
  for (int i = 0; i < 6; ++i) {
    char *p;
    a[i] = strtoul(inp, &p, 16);
    if (p == inp) return false;
    inp = p+1; // skip ':'
  }
  return true;
}

/**
 * ifconfig equivalent
 */
class IfCtl {
protected:
  int _sock; // control socket
  ifreq _ifr; // ifconfig

  void fail(const char *msg) {
    ERR("%s of %s: %s\n", msg, _ifr.ifr_name, strerror(errno));
  }

public:
  IfCtl(const char *iface) {
    _sock = socket(AF_INET, SOCK_DGRAM, 0); // should never fail
    strncpy(_ifr.ifr_name, iface, IFNAMSIZ);
    _ifr.ifr_name[IFNAMSIZ-1] = 0;
  }
  ~IfCtl() { close(_sock); }

  bool setState(bool up) { // set oper state
    if (ioctl(_sock, SIOCGIFFLAGS, &_ifr)) {
      fail("Could not get flags");
      return false;
    }
    if (up) _ifr.ifr_flags |= IFF_UP;
    else    _ifr.ifr_flags &= ~IFF_UP;
    if (ioctl(_sock, SIOCSIFFLAGS, &_ifr)) {
      fail("Could not set flags");
      return false;
    }
    return true;
  }

  /// net and mask in network order!
  bool setAddress(in_addr_t addr) {
    sockaddr_in *sin = (sockaddr_in *) &_ifr.ifr_addr;
    sin->sin_family = AF_INET;
    sin->sin_port = 0;
    sin->sin_addr.s_addr = addr;
    if (ioctl(_sock, SIOCSIFADDR, &_ifr)) {
      fail("Could not set address");
      return false;
    }
    return true;
  }

  bool setMask(in_addr_t mask) {
    sockaddr_in *sin = (sockaddr_in *) &_ifr.ifr_netmask;
    sin->sin_family = AF_INET;
    sin->sin_port = 0;
    sin->sin_addr.s_addr = mask;
    if (ioctl(_sock, SIOCSIFNETMASK, &_ifr)) {
      fail("Could not set netmask");
      return false;
    }
    return true;
  }

  bool setMTU(int mtu) {
    _ifr.ifr_mtu = mtu;
    if (ioctl(_sock, SIOCSIFMTU, &_ifr)) {
      fail("Could not set MTU");
      return false;
    }
    return true;
  }

  bool isUp() { // get oper state, NOTE: if iface missing it returns false too
    if (ioctl(_sock, SIOCGIFFLAGS, &_ifr)) {
      // no error message
      return false;
    }
    return _ifr.ifr_flags & IFF_UP;
  }

  in_addr_t getAddress() {
    if (ioctl(_sock, SIOCGIFADDR, &_ifr)) {
      fail("Could not get address");
      return INADDR_NONE;
    }
    return ((sockaddr_in *)&_ifr.ifr_addr)->sin_addr.s_addr;
  }

  in_addr_t getMask() {
    if (ioctl(_sock, SIOCGIFNETMASK, &_ifr)) {
      fail("Could not get netmask");
      return INADDR_NONE;
    }
    return ((sockaddr_in *)&_ifr.ifr_addr)->sin_addr.s_addr;
  }

  int getMTU() {
    if (ioctl(_sock, SIOCGIFMTU, &_ifr)) {
      fail("Could not get MTU");
      return -1;
    }
    return _ifr.ifr_mtu;
  }

  bool getHwAddress(uint8_t *addr) {
    if (ioctl(_sock, SIOCGIFHWADDR, &_ifr)) {
      return false;
    }
    memcpy(addr, _ifr.ifr_hwaddr.sa_data, 6);
    return true;
  }
};

#endif // INCLUDED_IFCTL_HH
