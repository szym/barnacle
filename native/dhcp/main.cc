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

#include <config.hh>
#include <ifctl.hh>
#include "dhcp.hh"

int main(int, const char **) {
  // configure, then run DHCP, bam!
  DHCP::Config c;

  // setup some defaults
  c.firsthost   = 100;
  c.numhosts    = 100;
  c.leasetime   = 1200;

  {
    using namespace Config;
    Param params[] = {
     { "brncl_if_lan",        new String(c.ifname, IFNAMSIZ), true },
     { "brncl_dhcp_dns1",     new IP(c.dns1),         false },
     { "brncl_dhcp_dns2",     new IP(c.dns2),         false },
     { "brncl_dhcp_leasetime",new Time(c.leasetime),  false },
     { "brncl_dhcp_firsthost",new Uint16(c.firsthost), false },
     { "brncl_dhcp_numhosts", new Uint16(c.numhosts), false },
     { 0, NULL, false }
    };
    if (!configure(params))
      return -2;
  }

  IfCtl ic(c.ifname);
  c.gw      = ic.getAddress();
  c.netmask = ic.getMask();
  c.subnet  = c.gw & c.netmask;

  DHCP dhcp(c);
  if (!dhcp.init())
    return -3;

  close(0); open("/dev/null", O_RDONLY);
  while(dhcp.run());
  ERR("DHCP exited: %s\n", strerror(errno));
  return -1;
}

