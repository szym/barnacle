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

#define TAG "NAT: "
#include <config.hh>
#include <ifctl.hh>
#include "barnacle.hh"

// sleep until the interfaces are available
void wait_for_if(const char *inif, const char *outif) {
  IfCtl ic_in(inif);
  IfCtl ic_out(outif);

  bool inup = ic_in.isUp();
  bool outup = ic_out.isUp();
  if (!inup || !outup) {
    // the interface is missing or not up, wait until it is
    LOG("waiting for %s interface\n", inup ? "uplink" : (outup ? "local" : "uplink and local"));
    while (!ic_in.isUp() || !ic_out.isUp()) sleep(1); // at most a second of lost connectivity
    LOG("restarting now\n");
  }
}

struct PortList : public Config::Parser {
  unsigned &num;
  uint16_t * &list;
  PortList(unsigned &n, uint16_t * &l) : num(n), list(l) {}
  bool parse(const char * arg) {
    num = 1;
    // count commas
    for (const char * s = arg; (s = strchr(s, ',')); ++num, ++s) ;
    list = new uint16_t[num];
    unsigned i = 0;
    for (const char * s = arg; i < num; ++s, ++i) {
      list[i] = strtoul(s, const_cast<char **>(&s), 10);
      if (*s != ',') {
        return !(*s);
      }
    }
    return false;
  }
};

void die(int) {
  exit(1);
}

int main(int, const char **) {
  prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0);
  struct sigaction act;
  act.sa_handler = die;
  sigaction(SIGTERM, &act, 0);

  // configure, then run barnacle, bam!
  Barnacle::Config c;

  // setup some defaults
  c.queuelen    = 100;
  c.numpreserved = 0;
  c.firstport   = 32000;
  c.numports    = 100;
  c.timeout     = 30;
  c.timeout_tcp = 90;
  c.log         = false;
  c.ctrl[0]     = '\0';

  {
    using namespace Config;
    Param params[] = {
     { "brncl_if_wan",        new String(c.outif, IFNAMSIZ), true },
     { "brncl_if_lan",        new String(c.inif, IFNAMSIZ),  true },
     { "brncl_nat_queue",     new Uint(c.queuelen),       false },
     { "brncl_nat_timeout",   new Time(c.timeout),        false },
     { "brncl_nat_timeout_tcp", new Time(c.timeout_tcp),  false },
     { "brncl_nat_numports",  new Uint(c.numports),       false },
     { "brncl_nat_firstport", new Uint16(c.firstport),    false },
     { "brncl_nat_log",       new Bool(c.log),            false },
     { "brncl_nat_ctrl",      new String(c.ctrl, UNIX_PATH_MAX), false },
     { "brncl_nat_preserve",  new PortList(c.numpreserved, c.preserved), false },
     { 0, NULL, false }
    };
    if (!configure(params))
      return -2;
  }

  close(0); open("/dev/null", O_RDONLY);

  Barnacle brncl(c);
  if (!brncl.init_ctrl()) {
    LOG("init_ctrl failed: %s\n", strerror(errno));
    return -1;
  }

  // we first wait until the interfaces are available...
  for(;;) {
    wait_for_if(c.inif, c.outif);

    if (!brncl.start()) {
      ERR("start: %s\n", strerror(errno));
      sleep(2); // to avoid spamming
      continue;
    }
    while (brncl.run());
    ERR("restart: %s\n", strerror(errno));
  }
  return -1;
}

