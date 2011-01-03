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

#ifndef INCLUDED_LOG_HH
#define INCLUDED_LOG_HH

#include <stdio.h>
#include <sys/file.h>

#ifndef TAG
#define TAG
#endif

#define ERR(...) { fprintf(stderr, TAG __VA_ARGS__); fflush(stderr); }
#define LOG(...) { flock(1, LOCK_EX); fprintf(stdout, TAG __VA_ARGS__); fflush(stdout); flock(1, LOCK_UN); }

#ifdef ANDROID
extern "C" {
#include <android/log.h>
}

#define LOG_LEVEL ANDROID_LOG_DEBUG
#define DBG(...) __android_log_print(LOG_LEVEL, "barnacle", TAG __VA_ARGS__)
#else
#define DBG LOG

#endif

#endif // INCLUDED_LOG_HH
