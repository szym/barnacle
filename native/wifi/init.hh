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

// init.rc parsing routines

#include <unistd.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>

#include <properties.hh>

namespace Init {
  const char * skipWhite(const char *s) {
    while (isblank(*s)) ++s;
    return s;
  }

  void chomp(char * s) {
    char * end = s + strlen(s);
    if (end > s) {
      do --end; while (isspace(*end));
      *(end+1) = 0;
    }
  }

  struct Service {
    const char *name;
    char *command;
    char *socket;

    pid_t pid;

    Service(const char *n) : name(n), command(0), socket(0) {}

    // service <name> <command>
    // socket <socket>
    void parseLine(const char* line) {
      if (!command) {
        if (strncmp(line, "service", 7) != 0)
          return;

        line = skipWhite(line + 7);
        if (!*line) return;

        int namelen = strlen(name);
        if (strncmp(line, name, namelen) == 0) {
          command = strdup(skipWhite(line + namelen));
          chomp(command);
        }
      } else if (!socket) {
        if (strncmp(line, "socket", 6) == 0) {
          socket = strdup(skipWhite(line + 6));
          chomp(socket);
        }
      }
    }

    bool parse(FILE *f) {
      char buf[1024];
      char *p = buf;

      while(!(command && socket)) {
        if (fgets(p, buf + sizeof(buf) - p, f) == 0)
          return false;
        char *b = strchr(p, '#');
        if (b) { // trim comment
          *b = 0;
        } else {
          b = strchr(p, '\\');

          if (b && ((b[1] == '\r') || (b[1] =='\n'))) {
            p = b;
            continue; // stitch next line
          }
        }
        p = buf;
        parseLine(skipWhite(buf));
      }
      return true;
    }

    ~Service() {
      if (command) { free(command); command = 0; }
      if (socket) { free(socket); socket = 0; }
    }

    int createSocket() {
      // parse the socket params <name> <type> <perm> <uid> <gid>
      char *name = strtok(socket, " ");
      char *stype = strtok(0, " ");
      char *sperm = strtok(0, " ");
      char *suid = strtok(0, " ");
      char *sgid = strtok(0, " ");

      if (!stype || !sperm) return -1;

      int type = (!strcmp(stype, "dgram")) ? SOCK_DGRAM : SOCK_STREAM;
      int perm = strtoul(sperm, 0, 8); // TODO: check errors

      uid_t uid = 0;
      gid_t gid = 0;

      if (suid) {
        struct passwd * p = getpwnam(suid);
        if (!p) return -1;
        uid = p->pw_uid;
        gid = p->pw_gid;

        if (sgid) {
          struct group * g = getgrnam(sgid);
          if (!g) return -1;
          gid = g->gr_gid;
        }
      }

      // see system/core/init/util.c:create_socket
      struct sockaddr_un addr;
      int fd, ret;

      fd = ::socket(PF_UNIX, type, 0);
      if (fd < 0) {
          DBG("Failed to open socket '%s': %s\n", name, strerror(errno));
          return -1;
      }

      memset(&addr, 0 , sizeof(addr));
      addr.sun_family = AF_UNIX;
      snprintf(addr.sun_path, sizeof(addr.sun_path), ANDROID_SOCKET_DIR"/%s", name);

      ret = ::unlink(addr.sun_path);
      if (ret != 0 && errno != ENOENT) {
        DBG("Failed to unlink old socket '%s': %s\n", name, strerror(errno));
        goto out_close;
      }

      ret = ::bind(fd, (struct sockaddr *) &addr, sizeof (addr));
      if (ret) {
        DBG("Failed to bind socket '%s': %s\n", name, strerror(errno));
        goto out_unlink;
      }

      ::chown(addr.sun_path, uid, gid);
      ::chmod(addr.sun_path, perm);

      DBG("Created socket '%s' with mode '%o', user '%d', group '%d'\n",
          addr.sun_path, perm, uid, gid);

      return fd;

out_unlink:
      ::unlink(addr.sun_path);
out_close:
      ::close(fd);
      return -1;
    }

    bool start() {
      // create socket
      int sock = createSocket();
      if (sock < 0) return false;

      pid = fork();
      if (pid < 0) return false;

      if (pid > 0) {
        close(sock); // leave it to the child

        char prop_name[PROP_NAME_MAX];
        snprintf(prop_name, PROP_NAME_MAX, "init.svc.%s", name);
        if (!property_set(prop_name, "running"))
          DBG("Failed to set init.svc. property %s\n", strerror(errno));

        return true;
      }

      setpgid(0, getpid());

      close(0); close(1); close(2);

      // publish socket
      char env_name[64];
      char env_val[64];
      snprintf(env_name, sizeof(env_name), ANDROID_SOCKET_ENV_PREFIX"%s", socket);
      snprintf(env_val, sizeof(env_val), "%d", sock);
      setenv(env_name, env_val, 1);

      // split into argc, argv
      int argc;
      char * argv[128];
      argv[0] = strtok(command, " ");
      for(argc = 1; argc < 128; ++argc) {
        argv[argc] = strtok(0, " ");
        if (!argv[argc]) {
          break;
        }
      }

      // execve
      if (execv(argv[0], argv) < 0) {
        DBG("Failed to execute %s\n", argv[0]);
      }
      _exit(127);
      return false; // unreachable
    }

    void stop() {
      if (pid > 0) {
        DBG("stopping %s\n", name);
        kill(-pid, SIGTERM);
        char prop_name[PROP_NAME_MAX];
        snprintf(prop_name, PROP_NAME_MAX, "init.svc.%s", name);
        property_set(prop_name, "stopping");
        pid = -1;
      }
    }
  };

  Service* find(const char * initrcname, const char * name) {
    FILE *initrc = fopen(initrcname, "r");
    if (!initrc) {
      DBG("Could not open %s\n", initrcname);
      return 0;
    }
    Service* s = new Service(name);
    if (!s->parse(initrc)) {
      DBG("Could not parse %s\n", initrcname);
      delete s;
      s = 0;
    }
    fclose(initrc);
    return s;
  }

  Service* find(const char * name) {
    Service *s = find("/init.rc", name);
    if (s) return s;

    const char * hardware = getenv("brncl_hardware");
    char buf[64];
    snprintf(buf, sizeof(buf), "/init.%s.rc", hardware);
    return find(buf, name);
  }
}
