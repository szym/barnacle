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

#ifndef INCLUDED_MACADDRESS_HH
#define INCLUDED_MACADDRESS_HH

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "hashcode.hh"

#include "ifctl.hh"
/*// FIXME: redefinition
static inline bool ether_parse(const char *inp, uint8_t *a) {
  for (int i = 0; i < 6; ++i) {
    char *p;
    a[i] = strtoul(inp, &p, 16);
    if (p == inp) return false;
    inp = p+1; // skip ':'
  }
  return true;
}
*/

struct MACAddress {
  uint8_t addr[6];
  MACAddress() {}
  MACAddress(const unsigned char *a) {
    memcpy(addr, a, 6);
  }
  bool operator==(const MACAddress &other) const {
    return memcmp(addr, other.addr, 6) == 0;
  }
  hashcode_t hashcode() const {
    const uint16_t *d = (const uint16_t *)addr;
    return (d[2] | (d[1] << 16)) ^ (d[0] << 9);
  }
  bool read(const char * s) {
    return ether_parse(s, addr);
  }
};
#endif // INCLUDED_MACADDRESS_HH
