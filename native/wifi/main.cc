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

#define TAG "WIFI: "
#include <config.hh>
#include "iwctl.hh"
#include "wifi.hh"

int config() {
  char  iflan[IFNAMSIZ];
  in_addr_t lan_gw = inet_addr("192.168.5.1");
  in_addr_t lan_netmask = inet_addr("255.255.255.0");

  {
    using namespace Config;
    Param params[] = {
     { "brncl_if_lan",      new String(iflan, IFNAMSIZ),  true },
     { "brncl_lan_gw",      new IP(lan_gw),               false },
     { "brncl_lan_netmask", new IP(lan_netmask),          false },
     { 0, NULL, false }
    };
    if (!configure(params))
      return -2;
  }

  IfCtl ic(iflan);

  bool success = (ic.setAddress(lan_gw)
               && ic.setMask(lan_netmask)
               && ic.setState(true));

  return success ? 0 : -1;
}

void cleanup() {
  Wifi::stop_supplicant();
}

int assoc_loop () {
  static const size_t BufSize = 64;

  char  iflan[IFNAMSIZ];
  char  essid[BufSize+1] = "barnacle";
  char  bssid[BufSize+1] = { '\0' };
  char  wep[BufSize+1] = { '\0' };
  unsigned channel = 1;
  bool  usewext = false;

  {
    using namespace Config;
    Param params[] = {
     { "brncl_if_lan",      new String(iflan, IFNAMSIZ), true },
     { "brncl_lan_essid",   new String(essid, BufSize),  false },
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
      // wireless extensions configuration

      if (!ic.setMode()) {
        ;//ERR("Failed to set ad-hoc mode\n");
      }
      if (bssid[0]) {
        uint8_t BSS[6];
        if (ether_parse(bssid, BSS)) {
          if (!ic.setBssid(BSS)) {
            ;//ERR("Failed to set BSSID\n");
          }
        } else ERR("Failed to parse BSSID\n");
      }

      if (wep[0]) {
        if (!ic.configureWep(wep))
          ERR("Failed to configure WEP\n");
      }
      ic.commit();

      uint8_t eth[6];
      ic.getHwAddress(eth);
      LOG("OK %s %02x:%02x:%02x:%02x:%02x:%02x\n", iflan,
          eth[0], eth[1], eth[2], eth[3], eth[4], eth[5]);

      // associate
      do {
        DBG("WLEXT assoc\n");
        if (!ic.setEssid(essid, strlen(essid)))
          break;
        // wait for line on stdin before attempting again
        fgets(buf, sizeof(buf), stdin);
      } while (!feof(stdin));

    } else {
      // wpa_supplicant configuration

      FILE *f = fopen("wpa.conf", "w");
      if (f) {
        fprintf(f, "ctrl_interface=%s\nap_scan=2\n", iflan);
        fclose(f);
      }

      atexit(cleanup);
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

      uint8_t eth[6];
      ic.getHwAddress(eth);
      LOG("OK %s %02x:%02x:%02x:%02x:%02x:%02x\n", iflan,
          eth[0], eth[1], eth[2], eth[3], eth[4], eth[5]);
      do {
        DBG("WPASUPP assoc\n");
        if (!Wifi::assoc())
          break;
        // wait for line on stdin before attempting again
        fgets(buf, sizeof(buf), stdin);
      } while (!feof(stdin));
    }
  }

  Wifi::shutdown();
  return 0;
}

void die(int) {
  Wifi::shutdown();
  ERR("killed\n");
  exit(1);
}

int main(int argc, const char * argv[]) {
  prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0);
  struct sigaction act;
  act.sa_handler = die;
  sigaction(SIGINT, &act, 0);
  sigaction(SIGTERM, &act, 0);

  if (argc == 2) {
    if (!strcmp(argv[1], "assoc")) {
      return assoc_loop();
    } else if (!strcmp(argv[1], "config")) {
      return config();
    } else if (!strcmp(argv[1], "load")) {
      return Wifi::load_driver() ? 0 : -1;
    } else if (!strcmp(argv[1], "unload")) {
      return Wifi::unload_driver() ? 0 : -1;
    }
  }
  ERR("Usage: %s assoc|load|unload\n", argv[0]);
  return -1;
}

