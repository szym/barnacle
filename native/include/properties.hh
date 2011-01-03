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

// see system/core/libcutils/properties.c
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>

// this is defined in system/core/include/cutils/sockets.h
#define ANDROID_SOCKET_ENV_PREFIX "ANDROID_SOCKET_"
#define ANDROID_SOCKET_DIR "/dev/socket"

static
bool property_set(const char *key, const char *value) {
    prop_msg msg;
    msg.cmd = PROP_MSG_SETPROP;
    strcpy((char*) msg.name, key);
    strcpy((char*) msg.value, value);

    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0)
      return false;

    sockaddr_un sa;
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, ANDROID_SOCKET_DIR "/" PROP_SERVICE_NAME, UNIX_PATH_MAX);
    if (connect(s, (sockaddr *)&sa, sizeof(sa)))
      return false;

    int r;
    while((r = send(s, &msg, sizeof(msg), 0)) < 0) {
      if((errno == EINTR) || (errno == EAGAIN)) continue;
      break;
    }
    close(s);

    return (r == sizeof(prop_msg));
}

