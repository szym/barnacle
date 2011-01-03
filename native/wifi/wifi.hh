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

#ifndef WIFI_HH_INCLUDED
#define WIFI_HH_INCLUDED

#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

#include <log.hh>

#include "init.hh"

extern "C" {
#include "hardware_legacy_stub.h"
}

namespace Wifi {
  bool load_driver() {
    if (wifi_load_driver() != 0) {
      ERR("Failed to load driver: %s\n", strerror(errno));
      return false;
    }

    LOG("driver loaded\n");
    return true;
  }

  bool unload_driver() {
    return wifi_unload_driver() == 0;
  }

  static Init::Service *svc = 0;

  bool start_supplicant() {
    // we try to start the supplicant by hand in order to swap the .conf file
    if (!svc)
      svc = Init::find("wpa_supplicant");
    if (svc) {
      DBG("Found service: '%s' socket: '%s'\n", svc->command, svc->socket);
      // use our config file
      int cmdlen = strlen(svc->command);
      char cwd[256];
      getcwd(cwd, sizeof(cwd));
      int newcmdlen = cmdlen + strlen(cwd) + 32;
      char *newcommand = (char *)malloc(newcmdlen); // malloc not new[] because Service free()s
      snprintf(newcommand, newcmdlen, "%s -c%s/%s", svc->command, cwd, "wpa.conf");
      svc->command = newcommand;
      if (svc->start()) {
        DBG("wpa_supplicant custom-started\n");
        return true;
      } else {
        delete svc;
        svc = 0;
      }
    }
    // but if not, we use libhardware_legacy
    return false;// ::wifi_start_supplicant() == 0;
  }

  void stop_supplicant() {
    if (svc)
      svc->stop();
    else
      ::wifi_stop_supplicant();
  }


  // from core/jni/android_net_wifi_Wifi.cpp
  bool doCommand(const char *cmd, char *replybuf, int replybuflen) {
    size_t len = replybuflen - 1;
    if (::wifi_command(cmd, replybuf, &len) != 0) {
      return false;
    } else {
      // Strip off trailing newline
      if (len > 0 && replybuf[len-1] == '\n') --len;
      replybuf[len] = '\0';
      return true;
    }
  }

  int doIntCommand(const char *cmd) {
    char reply[256];
    if (doCommand(cmd, reply, sizeof(reply))) {
      return atoi(reply);
    } else {
      return -1;
    }
  }

  bool doBoolCommand(const char *cmd, const char *expect = "OK") {
    char reply[256];
    if (doCommand(cmd, reply, sizeof(reply))) {
      if (strcmp(reply, expect) != 0)
      ERR("received unexpected reply '%s'\n", reply);

      return (strcmp(reply, expect) == 0);
    } else {
      return false;
    }
  }

  void shutdown() {
    ::wifi_close_supplicant_connection();
    stop_supplicant();
  }

  bool init() {
    if (!start_supplicant()) {
      ERR("Failed to start supplicant: %s\n", strerror(errno));
      return false;
    }
    LOG("wpa_supplicant started\n");

    // try to connect to supplicant 3 times
    bool connected = false;
    for (int i = 0; i < 3; ++i) {
      if (::wifi_connect_to_supplicant() == 0) {
        connected = true;
        break;
      }
      usleep(100000); // 100ms
    }
    if (!connected) {
      ERR("Failed to connect to supplicant: %s\n", strerror(errno));
      return false;
    }
    LOG("wpa_supplicant connected\n");

    if (!doBoolCommand("PING", "PONG")) {
      ERR("Failed to ping supplicant\n");
      return false;
    }

    doBoolCommand("DRIVER START", "OK"); // ignore response

    if (!doBoolCommand("AP_SCAN 2", "OK")) {
      ERR("Failed to set AP_SCAN\n");
      return false;
    }

    if (!doBoolCommand("DISCONNECT", "OK")) {
      ERR("Failed to DISCONNECT\n");
      return false;
    }

    return true;
  }

  // returns < 0 if failed
  int addNetwork() {
    return doIntCommand("ADD_NETWORK");
  }

  bool setup(int netid, const char *essid, const char *bssid, const char *wep, int freq) {
    char buf[256];

#define SET(X...) \
    snprintf(buf, sizeof(buf), X); \
    if (!doBoolCommand(buf, "OK")) { \
      ERR("Failed to %s", buf); \
      return false; \
    }

    SET("SET_NETWORK %d mode 1", netid);
    SET("SET_NETWORK %d key_mgmt NONE", netid);
    if (bssid[0])
      SET("SET_NETWORK %d bssid %s", netid, bssid);
    if (wep[0]) {
      SET("SET_NETWORK %d wep_key0 %s", netid, wep);
      SET("SET_NETWORK %d wep_tx_keyidx 0", netid);
    }
    SET("SET_NETWORK %d ssid \"%s\"", netid, essid); // NOTE: ascii must be in ""

    if(freq)
      SET("SET_NETWORK %d frequency %d", netid, freq); // this does not work on TIWLAN

    SET("SELECT_NETWORK %d", netid);
    return true;
  }

  static bool firstAttempt = true;

  bool assoc() {
    doBoolCommand("DRIVER POWERMODE 1", "OK"); // 0-AUTO, 1-ACTIVE

    if (!firstAttempt)
      doBoolCommand("SCAN"); // this allows to join an existing IBSS

    firstAttempt = false;

    if (doBoolCommand("REASSOCIATE", "OK")) {
#if 0
      char buf[256];

      while(false) { // FIXME: actually process events -- but when do these events stop?
        int nread = ::wifi_wait_for_event(buf, sizeof(buf));
        if (nread < 0) return false; // failed!
        // find space
        char *p = buf;
        for( ; *p && !isspace(*p); ++p);
        *p = '\0';
        if (strcmp(buf, "CTRL-EVENT-CONNECTED") == 0) {
          // connected! -- we're done
          return true;
        }
        if (strcmp(buf, "CTRL-EVENT-DISCONNECTED") == 0) {
          // disconnected! -- we're done!
           //-- or should we reassociate again?
          return true;
        }
        if (strcmp(buf, "CTRL-EVENT-TERMINATING") == 0) {
          // bam, this is not good
          return false;
        }
      }
#endif
      return true;
    }
    ERR("Failed to REASSOCIATE\n");
    return true; // FIXME: don't ignore this error
  }

};

#endif // WIFI_HH_INCLUDED
