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

/* Network I/O library for Barnacle: socket and selector */
#ifndef INCLUDED_SOCKET_HH
#define INCLUDED_SOCKET_HH

#include <string.h>
#include <errno.h>
#include <unistd.h> // for close
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
//#include <sys/un.h> // for sockaddr_un
#include <linux/un.h> // for sockaddr_un
#include <fcntl.h> // for O_NONBLOCK
#include <netinet/if_ether.h> // for ETHERTYPE_
#include <netinet/in.h> // for htons
#include <netpacket/packet.h> // for sockaddr_ll

#include <linux/filter.h> // for BPF_XX and sock_fprog

#include <netinet/ip.h> // for fixing IP_HDRINCL retardedness

#include <linux/if.h> // ifreq


#include <assert.h>

#include "buffer.hh"
#include "log.hh"

/* <RANT> szym:
 *  Linux support for user-space IP protocols is retarded. The choice is between
 *  (AF_PACKET, SOCK_RAW/SOCK_DGRAM) sockets that bypass the ARP (the user needs
 *  to implement it again!) and (AF_INET, SOCK_RAW) sockets that cannot be used
 *  to capture ALL possible IP protocols, but at least use kernel's ARP on the
 *  way out. Will have to overlook the fact that even with IP_HDRINCL, the
 *  (AF_INET, SOCK_RAW) sockets modify the included IP header, need a valid
 *  sockaddr_in despite the address already being included in the packet, and
 *  might end up being sent out on an unintended device! Also, the length
 *  included in the IP header is ignored! Sheesh!
 *
 *  The current workaround is to use AF_PACKET sockets for capture and AF_INET
 *  socket for injection.
 * </RANT>
 */

class BaseSocket {
protected:
  int _fd;
public:
  int  fd() const { return _fd; }
  bool ok() const { return _fd >= 0; }
  void close() { if (ok()) ::close(_fd); _fd = -1; }
};

/**
 * (AF_PACKET, SOCK_DGRAM) socket for capturing all IP packets.
 */
class PacketSocket : public BaseSocket {
public:
  PacketSocket() {
    // FIXME: change to SOCK_RAW if we need the Ethernet header
    _fd = ::socket(AF_PACKET, SOCK_DGRAM | O_NONBLOCK, htons(ETHERTYPE_IP));
  }

  bool bind(const char *iface, bool promisc = false) {
    // get interface index
    ifreq ifr;
    ::memset(&ifr, 0, sizeof(ifr));
    ::strncpy(ifr.ifr_name, iface, sizeof(ifr.ifr_name));
    if (::ioctl(_fd, SIOCGIFINDEX, &ifr))
      return false;
    sockaddr_ll sa;
    sa.sll_family = AF_PACKET;
    sa.sll_protocol = htons(ETHERTYPE_IP);
    sa.sll_ifindex = ifr.ifr_ifindex;

    if(::bind(_fd, (sockaddr *)&sa, sizeof(sa)))
      return false;
    if(promisc) { // is this even necessary?
      if (ioctl(_fd, SIOCGIFFLAGS, &ifr) != 0) return false;
      ifr.ifr_flags = ifr.ifr_flags | IFF_PROMISC;
      if (ioctl(_fd, SIOCSIFFLAGS, &ifr) != 0) return false;
    }
    return true;
  }

  /// return 0 on try again, -1 on fail
  int recv(Buffer &b) {
    b.clear();
    sockaddr_ll sll;
    socklen_t slen = sizeof(sll);
    int len = ::recvfrom(_fd, b.data(), b.room(), MSG_TRUNC, (sockaddr *)&sll, &slen);
    if (len > 0) {
      if(sll.sll_pkttype != PACKET_OUTGOING) {
        b.put(len);
        return len;
      }
      return 0;
    }
    return ((len < 0) && (errno == EAGAIN)) ? 0 : -1;
  }

#if 0
  int send(const Buffer &b, uint8_t *ethaddr) {
    sockaddr_ll sa;
    sa.sll_family = AF_PACKET;
    sa.sll_protocol = htons(ETHERTYPE_IP);
    sa.sll_halen = ETHER_ADDR_LEN;
    ::memcpy(sa.sll_addr, ethaddr, ETHER_ADDR_LEN);
    return ::sendto(_fd, b.data(), b.size(), 0, (sockaddr *)&sa, sizeof(sa));
  }
#endif
};

/**
 * (AF_INET, SOCK_RAW) socket for sending raw IP packets (without knowing MAC addresses)
 */
class IPSocket : public BaseSocket {
public:
  IPSocket() {
    _fd = ::socket(AF_INET, SOCK_RAW | O_NONBLOCK, IPPROTO_TCP /*RAW*/);
    if (_fd >= 0) {
      int val = 1;
      if (::setsockopt(_fd, IPPROTO_IP, IP_HDRINCL, &val, sizeof(val)) != 0)
        close();
    }
  }

  bool bind() {
    // get interface index
    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = 0;
    sa.sin_addr.s_addr = INADDR_ANY; // bind to all interfaces
    if(::bind(_fd, (sockaddr *)&sa, sizeof(sa)))
      return false;
    return true;
  }

  /// return 0 on try again, -1 on fail
  int send(const Buffer &b) {
    // fixing IP_HDRINCL retardedness
    const iphdr *ip = (const iphdr *)b.data();
    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = 0; // ignored in kernel
    sa.sin_addr.s_addr = ip->daddr; //I wish I could say INADDR_ANY
    int tot_len = ntohs(ip->tot_len);
    if ((unsigned)tot_len > b.size()) { // FIXME: move this to Rewriter
      DBG("IP HDR FAILS LEN CHECK %d %d\n", tot_len, b.size());
      return 1; // packet ignored!
    }
    int len = ::sendto(_fd, b.data(), tot_len, 0, (sockaddr *)&sa, sizeof(sa));
    //int len = ::send(_fd, b.data(), b.size(), 0); // EDESTADDRREQ!
    if (len < tot_len) {
      DBG("This is not good %d < %d\n", len, tot_len);
    }
    if (len > 0) {
      return len;
    }
    return ((len < 0) && (errno == EAGAIN)) ? 0 : -1;
  }

#if 0
  bool recv(Buffer &b) {
    b.clear();
    sockaddr_in sin;
    socklen_t slen = sizeof(sin);
    int len = ::recvfrom(_fd, b.data(), b.room(), MSG_TRUNC, (sockaddr *)&sin, &slen);
    return (len > 0);
  }
#endif
};

/**
 * (AF_INET, SOCK_STREAM) listening socket used to plug TCP ports
 * or
 * (AF_INET, SOCK_DGRAM) bound socket used to plug UDP ports
 */
class PlugSocket : public BaseSocket {
public:
  bool plug(uint16_t port, bool tcp = true) {
    _fd = tcp ? ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) :
                ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = port; // in network order
    sa.sin_addr.s_addr = 0;
    if(::bind(_fd, (sockaddr *)&sa, sizeof(sa))) {
      close();
      return false;
    }
    if (!tcp)
      return true;
    // and now we stuff the socket with a filter that always fails
    sock_filter filt[] = { BPF_STMT(BPF_RET|BPF_A, 0) };
    sock_fprog prog = { 1, filt };
    if(::setsockopt(_fd, SOL_SOCKET, SO_ATTACH_FILTER, &prog, sizeof(prog))) {
      close();
      return false;
    }
    if (::listen(_fd, 0)) {
      close();
      return false;
    }
    return true;
  }
};

/** //TODO: convert to SOCK_DGRAM
 * (AF_UNIX, SOCK_STREAM) socket for the control interface
 * NOTE: this is both the listening server socket and receiving socket
 */
class LocalSocket : public BaseSocket {
protected:
  LocalSocket(int fd) { _fd = fd; }
public:
  LocalSocket() {
    _fd = ::socket(AF_UNIX, SOCK_STREAM | O_NONBLOCK, 0);
  }

  bool listen(const char *path) {
    sockaddr_un sa;
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path, UNIX_PATH_MAX);
    return (::bind(_fd, (sockaddr *)&sa, sizeof(sa)) == 0)
        && (::listen(_fd, 0) == 0);
  }

  // NOTE: the accepted socket is set to non-blocking
  LocalSocket accept() {
    int fd = ::accept(_fd, NULL, 0);
    if (fd >= 0) {
      int flags = ::fcntl(fd, F_GETFL, 0);
      if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        ::close(fd);
        fd = -1;
      }
    }
    return LocalSocket(fd);
  }

  class Message : public Buffer {
  public:
    typedef uint8_t HdrType;
    static const unsigned HdrSize = sizeof(HdrType);
    const char *msg() const { return _buf + HdrSize; }
    // msg size does not include the header
    unsigned msg_size() const { return *(HdrType *)_buf; }
    unsigned to_read() const {
      return HdrSize + (_size < HdrSize ? 0 : msg_size()) - _size;
    }
    bool is_complete() const { return _size >= msg_size() + HdrSize; }
    void terminate() { _buf[HdrSize + msg_size()] = '\0'; }
  };


  // return > 0 if got whole message, 0 if again, -1 if failed
  int recv(Message &b) {
    unsigned toread = b.to_read();
    int len = ::recv(_fd, b.tail(), toread, 0);
    if (len > 0) {
      b.put(len);
      if (b.is_complete()) {
        b.terminate();
        return b.msg_size();
      }
      return 0;
    }
    return ((len < 0) && (errno == EAGAIN)) ? 0 : -1;
  }
};


/**
 * Trivial wrapper for select()
 */
class Selector {
protected:
  fd_set _fds_read;
  fd_set _fds_write;
  int _nfds;
public:
  Selector() { clear(); }
  void clear() { FD_ZERO(&_fds_read); FD_ZERO(&_fds_write); _nfds = 0; }
  void newFd(int fd) {
    if (fd >= _nfds) _nfds = fd + 1;
  }
  void wantRead(int fd, bool yup) {
    if(yup) FD_SET(fd, &_fds_read);
    else    FD_CLR(fd, &_fds_read);
  }
  void wantWrite(int fd, bool yup) {
    if(yup) FD_SET(fd, &_fds_write);
    else    FD_CLR(fd, &_fds_write);
  }
  bool canRead(int fd) const  { return FD_ISSET(fd, &_fds_read); }
  bool canWrite(int fd) const { return FD_ISSET(fd, &_fds_write); }
  int select() { return ::select(_nfds, &_fds_read, &_fds_write, NULL, NULL); }
};

#endif // INCLUDED_SOCKET_HH

