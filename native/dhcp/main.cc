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


#define TAG "DHCP: "
#include <config.hh>
#include <ifctl.hh>
#include "dhcp.hh"

void die(int) {
  exit(1);
}

int main(int, const char **) {
  prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0);
  struct sigaction act;
  act.sa_handler = die;
  sigaction(SIGTERM, &act, 0);

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
  if (c.gw == INADDR_NONE)
      return -3;

  c.netmask = ic.getMask();
  if (c.netmask == INADDR_NONE)
      return -3;

  c.subnet  = c.gw & c.netmask;


  DHCP dhcp(c);
  if (!dhcp.init())
    return -3;

  close(0); open("/dev/null", O_RDONLY);
  while(dhcp.run());
  ERR("exited: %s\n", strerror(errno));
  return -1;
}

