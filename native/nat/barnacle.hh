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

#ifndef INCLUDED_BARNACLE_HH
#define INCLUDED_BARNACLE_HH

#include <time.h>

#define NAT_OPEN
#ifdef NAT_OPEN
#include "natopen.hh"
#else
#include "natsym.hh"
#endif
#include "filtersocket.hh"

class Barnacle {
public:
  struct Config : public Rewriter::Config {
    char      outif[IFNAMSIZ];
    char      inif[IFNAMSIZ];
    unsigned  queuelen;
    time_t    timeout; // in seconds (UDP and ICMP traffic)
    time_t    timeout_tcp; // in seconds (TCP only)
    char      ctrl[UNIX_PATH_MAX]; // for control
  };
protected:
  Config _cfg;

  // control interface
  LocalSocket   _ctrl_server; // listening for connections
  LocalSocket   _ctrl; // the currently opened control socket
  LocalSocket::Message _msg;

  PacketSocket  _outs;  // ppp capture
  FilterSocket  _ins;   // wifi capture
  IPSocket      _ips;   // injection
  Queue<Buffer> _q;     // injection

  Selector      _sel;

  Rewriter      _rw;
  time_t        _lastcleanup;     // time of last cleanup
  time_t        _lastcleanup_tcp; // time of last cleanup

  int _mtu;

  // stats
  int _nin, _nout, _bin, _bout; // FIXME: remove

  bool have_ctrl() { return _cfg.ctrl[0] != '\0'; }
  void handle_ctrl();
  bool handle_in();
  bool handle_out();
  bool drain();
  void cleanup();

public:
  Barnacle(const Config &c) : _cfg(c), _q(c.queuelen), _rw(c) { }
  ~Barnacle();

  // configure ctrl
  bool init_ctrl();
  // configure the rest of I/O
  bool start();
  // return false on I/O failure
  bool run();
};

#endif //INCLUDED_BARNACLE_HH

