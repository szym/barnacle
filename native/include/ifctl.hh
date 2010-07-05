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

/**
 * ifconfig equivalent
 */
class IfCtl {
protected:
  int _sock; // control socket
  ifreq _ifr; // ifconfig

  void fail(const char *msg) {
    ERR(msg, _ifr.ifr_name, strerror(errno));
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
      fail("Could not get flags of %s: %s\n");
      return false;
    }
    if (up) _ifr.ifr_flags |= IFF_UP;
    else    _ifr.ifr_flags &= ~IFF_UP;
    if (ioctl(_sock, SIOCSIFFLAGS, &_ifr)) {
      fail("Could not set flags of %s: %s\n");
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
      fail("Setting address of %s failed: %s\n");
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
      fail("Setting netmask of %s failed: %s\n");
      return false;
    }
    return true;
  }

  in_addr_t getAddress() {
    if (ioctl(_sock, SIOCGIFADDR, &_ifr)) {
      fail("Getting address of %s failed: %s\n");
      return false;
    }
    return ((sockaddr_in *)&_ifr.ifr_addr)->sin_addr.s_addr;
  }

  in_addr_t getMask() {
    if (ioctl(_sock, SIOCGIFNETMASK, &_ifr)) {
      fail("Getting netmask of %s failed: %s\n");
      return false;
    }
    return ((sockaddr_in *)&_ifr.ifr_addr)->sin_addr.s_addr;
  }
};

#endif // INCLUDED_IFCTL_HH
