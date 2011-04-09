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

/* Network I/O library for Barnacle: buffer and queue */
#ifndef INCLUDED_BUFFER_HH
#define INCLUDED_BUFFER_HH

#include <string.h> // for memset
#include <assert.h>

/**
 * Fixed size buffer for IP packets
 */
template <unsigned MaxSize = 2048>
class BufferT {
  BufferT(const BufferT &b); // no copying allowed
  BufferT &operator=(const BufferT &b);
protected:
  char _buf[MaxSize];
  unsigned _size;
public:
  BufferT() : _size(0) {}
  void clear() { ::memset(_buf, 0, MaxSize); _size = 0; }
  char *data() { return _buf; }
  char *tail() { return _buf + _size; }
  const char *data() const { return _buf; }
  unsigned size() const { return _size; }
  unsigned room() const { return MaxSize - _size; }
  void put(unsigned n) { _size+= n; assert(_size < MaxSize); }
  void trim(unsigned n) { assert(n < _size); _size = n; }
};

typedef BufferT<> Buffer;


/**
 * A fixed-size circular queue of buffers (or whatever else)
 */
template <typename T = Buffer >
class Queue {
protected:
  unsigned Num;
  T *_buf;
  unsigned _head;
  unsigned _tail;
public:
  Queue(unsigned size) : Num(size+1), _buf(new T[Num]), _head(0), _tail(0) {}
  ~Queue() { if(_buf) delete [] _buf; _buf = 0; }
  /// next packet to read from the queue
  T &head() { assert(!empty()); return _buf[_head]; }
  /// place to add to the queue
  T &tail() { assert(!full()); return _buf[_tail]; }
  unsigned size() const { return (_tail + Num - _head) % Num; }
  unsigned maxsize() const { return Num - 1; }
  /// is the tail unavailable? -- same as size == Num-1
  bool full() { return (_tail + 1) % Num == _head; }
  /// is the head unavailable?
  bool empty() { return _tail == _head; }
  void popHead() { assert(!empty()); _head = (_head + 1) % Num; }
  void pushTail() { assert(!full()); _tail = (_tail + 1) % Num; }
  void clear() { _head = _tail; }
};

#endif // INCLUDED_BUFFER_HH
