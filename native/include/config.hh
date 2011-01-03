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

#ifndef INCLUDED_CONFIG_HH
#define INCLUDED_CONFIG_HH

#include <linux/prctl.h> // for prctl

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>
#include "log.hh"

namespace Config {
  struct Parser {
    virtual bool parse(const char *arg) = 0;
  };

  template<typename T>
  struct Unsigned : public Parser {
    T &param;
    Unsigned(T &p) : param(p) {}
    bool parse(const char * arg) {
      char *p;
      unsigned long int val = strtoul(arg, &p, 10);
      if (p == arg) return false;
      param = (T)val;
      return true;
    }
  };

  typedef Unsigned<unsigned>    Uint;
  typedef Unsigned<uint8_t>     Uint8;
  typedef Unsigned<uint16_t>    Uint16;
  typedef Unsigned<uint32_t>    Uint32;
  typedef Unsigned<time_t>      Time;
  typedef Unsigned<bool>        Bool;

  struct String : public Parser {
    char *param;
    size_t sz;
    String(char *p, size_t s) : param(p), sz(s) {}
    bool parse(const char * arg) {
      // TODO: check if arg not over sz
      strncpy(param, arg, sz);
      return true;
    }
  };

  struct IP : public Parser {
    in_addr_t &param;
    IP(in_addr_t &p) : param(p) {}
    bool parse(const char * arg) {
      in_addr_t val = inet_addr(arg);
      if (val == INADDR_NONE) return false;
      param = val;
      return true;
    }
  };

  struct Param {
    const char *name;
    Parser *parser;
    bool required;

    bool assign() {
      const char * val = getenv(name);
      if ((val == NULL) || !strlen(val)) {
        if (!required) {
          return true;
        }
        ERR("Parameter %s is unset\n", name);
        return false;
      }
      if (!parser->parse(val)) {
        ERR("Failed to parse '%s' for %s\n", val, name);
        return false;
      }
      return true;
    }
    ~Param() { if (parser) delete parser; parser = NULL; }
  };

  /**
   * Usage:
   *   int p;
   *   Param params[] = {
   *    { "PARAM_NAME", new Unsigned(p), true },
   *    { "OPTIONAL_PARAM_NAME", new String<20>(p) },
   *    { 0 }
   *   };
   *   return configure(params);
   */
  bool configure(Param params[]) {
    for (Param *p = params; p->name; ++p) {
      if (!p->assign())
        return false;
    }
    return true;
  }

}

#endif // INCLUDED_CONFIG_HH

