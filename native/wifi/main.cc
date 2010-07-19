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
#include "iwctl.hh"
#include "wifi.hh"

int configure() {

  char  iflan[IFNAMSIZ];
  in_addr_t lan_gw;
  in_addr_t lan_netmask;

  {
    using namespace Config;
    Param params[] = {
     { "brncl_if_lan",      new String(iflan, IFNAMSIZ),  true },
     { "brncl_lan_gw",      new IP(lan_gw),               true },
     { "brncl_lan_netmask", new IP(lan_netmask),          true },
     { 0, NULL, false }
    };
    if (!configure(params))
      return -2;
  }
  IfCtl ic(iflan);
  return (ic.setAddress(lan_gw) && ic.setMask(lan_netmask) && ic.setState(true)) ? 0 : -1;
}


int assoc_loop () {
  static const size_t BufSize = 64;

  char  iflan[IFNAMSIZ];
  char  essid[BufSize+1];
  char  bssid[BufSize+1] = { '\0' };
  char  wep[BufSize+1] = { '\0' };
  unsigned channel = 1;
  bool  usewext = false;

  {
    using namespace Config;
    Param params[] = {
     { "brncl_if_lan",      new String(iflan, IFNAMSIZ), true },
     { "brncl_lan_essid",   new String(essid, BufSize),  true },
     { "brncl_lan_bssid",   new String(bssid, BufSize),false },
     { "brncl_lan_wep",     new String(wep, BufSize),  false },
     { "brncl_lan_channel", new Uint(channel),         false },
     { "brncl_lan_wext",    new Bool(usewext),         false },
     { 0, NULL, false }
    };
    if (!configure(params))
      return -2;
  }

  char buf[1024];

  IwCtl ic(iflan);
  if(ic.setState(true)) { // just in case
    ic.setChannel(channel); // ignore return value

    if (usewext) {
      if (!ic.setMode()) {
        ERR("Failed to set ad-hoc mode\n");
      }
      if (bssid[0]) {
        uint8_t BSS[6];
        if (ether_parse(bssid, BSS)) {
          if (!ic.setBssid(BSS))
            ERR("Failed to set BSSID\n");
        } else ERR("Failed to parse BSSID\n");
      }
      if (wep[0]) {
        uint8_t WEP[WepSize];
        if (!wep_parse(wep, WEP))
          ERR("Failed to parse WEP\n"); // FIXME: accept 40-bit wep too
        if (!ic.configureWep(WEP))
          ERR("Failed to configure WEP\n");
      }
      ic.commit();
      // associate
      LOG("OK %s\n", iflan);
      do {
        if (!ic.setEssid(essid, strlen(essid)))
          break;
        // wait for line on stdin before attempting again
        fgets(buf, sizeof(buf), stdin);
      } while (!feof(stdin));
    } else {
      if (!Wifi::init())
        return -1;

      int netid = Wifi::addNetwork();
      if (netid < 0) {
        ERR("Failed to ADD_NETWORK\n");
        return -1;
      }
      // TODO: allow dynamic reconfiguration in the association loop
      if (!Wifi::setup(netid, essid, bssid, wep, chan2freq(channel)))
        return -1;
      LOG("OK %s\n", iflan);
      do {
        if (!Wifi::assoc())
          break;
        // wait for line on stdin before attempting again
        fgets(buf, sizeof(buf), stdin);
      } while (!feof(stdin));
    }
  }

  // TODO: sigaction SIGINT SIGQUIT SIGTERM shutdown
  Wifi::shutdown();
  return 0;
}

int main(int argc, const char * argv[]) {
  if (argc == 2) {
    if (!strcmp(argv[1], "assoc")) {
      return assoc_loop();
    } else if (!strcmp(argv[1], "config")) {
      return configure();
    } else if (!strcmp(argv[1], "load")) {
      return Wifi::load_driver() ? 0 : -1;
    } else if (!strcmp(argv[1], "unload")) {
      return Wifi::unload_driver() ? 0 : -1;
    }
  }
  ERR("Usage: %s assoc|load|unload\n", argv[0]);
  return -1;
}

