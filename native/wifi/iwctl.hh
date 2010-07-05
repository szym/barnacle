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

#ifndef INCLUDED_IWCTL_HH
#define INCLUDED_IWCTL_HH

#include <ctype.h>
#include <linux/wireless.h> // the most painful header ever
#include <netinet/if_ether.h> // for ETHERTYPE_ and ARPHRD

#include <arpa/inet.h> // for inet_addr

#include <ifctl.hh>


const int WepSize = 13; // 104 bits
static inline bool wep_parse(const char *inp, uint8_t *var) {
  if (strlen(inp) != 2*WepSize)
    return false;
  for (int i = 0; i < WepSize; ++i) {
    // so lazy, we'll use strtoul
    char b[3] = { inp[2*i], inp[2*i+1], 0 };
    char *p;
    var[i] = strtoul(b, &p, 16);
    if (p == b) return false;
  }
  return true;
}

static inline int chan2freq(int chan) {
  return 2407 + chan * 5; // MHz
}


/**
 * combines ifconfig and iwconfig
 */
class IwCtl : public IfCtl {
protected:
  // TODO: add event socket?
  iwreq _iwr; // iwconfig

  bool _setChannel(int chan) {
    _iwr.u.freq.m = chan;
    _iwr.u.freq.i = 0;
    _iwr.u.freq.e = 3; // kHz
    if (ioctl(_sock, SIOCSIWFREQ, &_iwr)) {
      DBG("Could not set freq: %s\n", strerror(errno));
      return false;
    }
    return true;
  }

public:
  IwCtl(const char *iface) : IfCtl(iface) {
    strncpy(_iwr.ifr_name, iface, IFNAMSIZ);
    _iwr.ifr_name[IFNAMSIZ-1] = 0;
  }

  bool setMode(int mode = IW_MODE_ADHOC) {
    _iwr.u.mode = mode;
    if (ioctl(_sock, SIOCSIWMODE, &_iwr)) {
      fail("Could not set ad-hoc mode of %s: %s\n");
      return false;
    }
    return true;
  }

  bool setChannel(int chan) {
    // TIWLAN is broken and thinks freq is channel idx
    return _setChannel(chan) || _setChannel(chan2freq(chan));
  }

  bool setBssid(uint8_t *bssid) {
    _iwr.u.ap_addr.sa_family = ARPHRD_ETHER;
    memcpy(_iwr.u.ap_addr.sa_data, bssid, ETHER_ADDR_LEN);
    if (ioctl(_sock, SIOCSIWAP, &_iwr)) {
      fail("Could not set bssid of %s: %s\n");
      return false;
    }
    return true;
  }

  bool setEssid(char *essid, int len) {
    // --> initiates association
    if (len < 0)
      len = strlen(essid);
    if (len > IW_ESSID_MAX_SIZE) {
      len = IW_ESSID_MAX_SIZE;
    }
    _iwr.u.essid.flags = 1; // not promiscuous
    _iwr.u.essid.length = len;
    _iwr.u.essid.pointer = (caddr_t) essid;
    if (ioctl(_sock, SIOCSIWESSID, &_iwr)) {
      fail("Could not set ssid of %s: %s\n");
      return false;
    }
    return true;
  }

  bool setAuthParam(int idx, uint32_t value) {
    _iwr.u.param.flags = idx & IW_AUTH_INDEX;
    _iwr.u.param.value = value;
    if (ioctl(_sock, SIOCSIWAUTH, &_iwr) < 0) {
      DBG("Could not set auth param: %s\n", strerror(errno));
      return false;
    }
    return true;
  }

  bool setWepKey(int idx, uint8_t *key) {
    _iwr.u.encoding.flags = (idx + 1) & IW_ENCODE_INDEX;
    if (key) {
      _iwr.u.encoding.pointer = (caddr_t) key;
      _iwr.u.encoding.length = WepSize;
    } else {
      _iwr.u.encoding.flags |= IW_ENCODE_DISABLED;
      _iwr.u.encoding.pointer = (caddr_t) NULL;
      _iwr.u.encoding.length = 0;
    }
    _iwr.u.data = _iwr.u.encoding;
    if (ioctl(_sock, SIOCSIWENCODE, &_iwr) < 0) {
      ERR("Could not set WEP key %d: %s\n", idx, strerror(errno));
      return false;
    }
    return true;
  }

  bool setWepTx(int idx) {
    _iwr.u.encoding.flags = (idx + 1) & IW_ENCODE_INDEX;
    _iwr.u.encoding.pointer = (caddr_t) NULL;
    _iwr.u.encoding.length = 0;
    _iwr.u.data = _iwr.u.encoding;
    if (ioctl(_sock, SIOCSIWENCODE, &_iwr) < 0) {
      ERR("Could not select WEP TX key %d: %s\n", idx, strerror(errno));
      return false;
    }
    return true;
  }

  bool disableWPA() {
    // disable WPA
    setAuthParam(IW_AUTH_WPA_VERSION, IW_AUTH_WPA_VERSION_DISABLED);
    setAuthParam(IW_AUTH_WPA_ENABLED, false);
    if (!setAuthParam(IW_AUTH_KEY_MGMT, 0)) {
      fail("Could not set KEY_MGMT to NONE on %s: %s\n");
      return false;
    }
    setAuthParam(IW_AUTH_PRIVACY_INVOKED, false);

//    setAuthParam(IW_AUTH_CIPHER_GROUP, IW_AUTH_CIPHER_NONE);
//    setAuthParam(IW_AUTH_CIPHER_PAIRWISE, IW_AUTH_CIPHER_NONE);
//    setAuthParam(IW_AUTH_RX_UNENCRYPTED_EAPOL, true);
    return true;
  }

  bool configureWep(uint8_t *key) {
    return disableWPA()
        && setWepKey(0, key)
        ;//&& setWepTx(0);
  }

  // TODO: monitor WLAN events
};

// find an interface interface in /proc/net/wireless or /proc/net/dev
bool find_if(const char *filename, int nlines, char *ifname) {
  FILE *f = fopen(filename, "r");
  if (f) {
    char buf[512];
    for (int i = 0; i < nlines; ++i)
      if (!fgets(buf, 512, f)) return false;

    // eat up leading spaces
    char *ps = buf;
    while(isspace(*ps)) ++ps;
    // find delimiter
    char *pe = ps;
    while(isalnum(*pe)) ++pe;
    *pe = '\0';
    strncpy(ifname, ps, IFNAMSIZ-1);
    fclose(f);
    return true;
  }
  return false;
}


#endif // INCLUDED_IWCTL_HH
