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

#ifndef INCLUDED_HASHTABLE_HH
#define INCLUDED_HASHTABLE_HH

#include <assert.h>
#include "hashcode.hh"

#define unlikely(x)     __builtin_expect((x),0)

template <typename T>
void swap(T& a, T& b) {
  T c = b; b = a; a = c;
}

template <typename T>
struct HashAdapter { /// makes simple types compatible with hashtable
  T u;
  HashAdapter() : u() {}
  HashAdapter(const T &v_) : u(v_) {}
  typedef T key_type;
  const T & key() const { return u; }
  operator T() const { return u; }
  // NOTE: op= is not allowed as it would change the key!
};

/**
 * A chained list hashtable.
 * type T {
 *   typedef key_type
 *   const key_type &key() const
 *   T(const key_type &)
 * }
 */
template <typename T>
class HashTable {
public:
  typedef typename T::key_type key_type;
protected:
  struct elt_t { // element in a chain
    T v;
    elt_t *next; /// the chain
    elt_t(const T &v_) : v(v_) {}
    const key_type &key() const { return v.key(); }
    // void *operator new(size_t s); // TODO: support allocator
    // void operator delete(void *p);
  };

  T _default; // for const operator[]

  elt_t **_buckets;
  size_t _nbuckets;
  mutable size_t _first_bucket; /// first non-empty or _nbuckets
  size_t _size; /// number of elements in the hashtable

  size_t bucket(const key_type &key) const {
    return ((size_t) hashcode(key)) % _nbuckets;
  }

  size_t min_size(size_t n) {
    size_t sz = 1;
    while (sz < n + 1) sz <<= 1;
    return sz - 1;
  }

  /// internal iterator over elt_ts
  struct elt_iterator {
    elt_t *_e; // == *_pe, except when end()
    elt_t **_pe; // address of what's pointing at _e (if not null)
    size_t _bucket;
    typedef const HashTable table_t;
    table_t *_ht;

    elt_iterator() {} // uninitialized

    inline elt_iterator(table_t *ht) : _ht(ht) { // begin()
      _bucket = ht->_first_bucket;
      _pe = &ht->_buckets[_bucket];
      if (unlikely(_bucket == ht->_nbuckets)) { // empty
        assert(ht->_size == 0);
        _e = 0; // end()
      } else if (!(_e = *_pe)) {
        ++(*this);
        ht->_first_bucket = _bucket;
      }
    }

    elt_iterator(table_t *ht, size_t b, elt_t **pe, elt_t *e)
        : _e(e), _pe(pe), _bucket(b), _ht(ht) {
      assert(!_e || (_pe && (*_pe == _e)));
    }

    elt_t *get() const { return _e; }
    bool live() const { return _e != 0; }
    void operator++() {
      if (_e && _e->next) { // same chain
        _pe = &(_e->next);
        _e = *_pe;
      } else if (_bucket != _ht->_nbuckets) { // next bucket
        for (++_bucket; _bucket != _ht->_nbuckets; ++_bucket)
          if (*(_pe = &_ht->_buckets[_bucket])) {
            _e = *_pe;
            return;
          }
        _e = 0; // end()
      }
    }
    void operator++(int) { ++*this; }
  };

  /// returns end() if none find
  elt_iterator elt_find(const key_type &key) const {
    size_t b = bucket(key);
    elt_t **pe;
    for (pe = &_buckets[b]; *pe; pe = &(*pe)->next) {
      if ((*pe)->key() == key) {
        return elt_iterator(this, b, pe, *pe);
      }
    }
    return elt_iterator(this, b, &_buckets[b], 0); // special end()
  }

  /// returns previously stored element
  elt_t *elt_set(elt_iterator &it, elt_t *e) {
    assert((it._ht == this) && (it._bucket < _nbuckets));
    assert(!e || (bucket(e->key()) == it._bucket));

    elt_t *old = it.get();
    if (unlikely(old == e))
      return old;

    if (!e) { // erasing
      --_size;
      if (!(*it._pe = it._e = old->next)) // chain got empty
        ++it;
      return old;
    }
    if (old) { // replacing
      e->next = old->next;
    } else { // inserting
      ++_size;
      if (unlikely(unbalanced())) {
        rehash(_nbuckets + 1);
        it._bucket = bucket(e->key());
        it._pe = &_buckets[it._bucket];
      }
      if (!(e->next = *it._pe))
        _first_bucket = 0;
    }
    *it._pe = it._e = e;
    return old;
  }

  //friend void swap(HashTable<T> &a, HashTable<T> &b);

public:
  /// n = number of buckets
  HashTable(size_t n = 63) : _nbuckets(min_size(n)),
      _first_bucket(_nbuckets), _size(0) {
    _buckets = new elt_t*[_nbuckets];
    for (size_t b = 0; b < _nbuckets; ++b) _buckets[b] = 0;
  }

  ~HashTable() { clear();
    delete [] _buckets;
    _buckets = 0;
  }

  size_t size() const { return _size; }
  bool empty() const { return _size == 0; }
  bool unbalanced() const { return _size > 2 * _nbuckets; }

  void rehash(size_t n) {
    size_t new_nbuckets = min_size(n);
    if (_nbuckets == new_nbuckets) return; // noop

    size_t old_nbuckets = _nbuckets;
    elt_t **old_buckets = _buckets;

    _nbuckets = new_nbuckets;
    _buckets = new elt_t*[_nbuckets];
    for (size_t b = 0; b < _nbuckets; ++b) _buckets[b] = 0;

    for (size_t b = 0; b < old_nbuckets; b++)
      for (elt_t *e = old_buckets[b]; e; ) {
        elt_t *next = e->next;
        size_t new_b = bucket(e->key());
        e->next = _buckets[new_b];
        _buckets[new_b] = e;
        e = next;
      }

    delete [] old_buckets;
    _first_bucket = 0;
  }

  /// iterators
  class const_iterator {
  protected:
    friend class HashTable;
    elt_iterator _rep;
    const_iterator(const elt_iterator &i) : _rep(i) {}
  public:
    const_iterator() { }
    const T *get() const        { return _rep.live() ? &_rep.get()->v : 0; }
    const T &operator*() const  { assert(_rep.live()); return _rep.get()->v; }
    const T *operator->() const { return &(*(*this)); }

    bool live() const { return _rep.live(); }
    void operator++(int) { _rep++; }
    void operator++() { ++_rep; }

    /// operator == so that we can compare to end()
    bool operator==(const const_iterator &other) const { return this->get() == other.get(); }
    bool operator!=(const const_iterator &other) const { return !(*this == other); }
  };
  /// same as const_iterator except it's not const
  class iterator : public const_iterator {
  protected:
    friend class HashTable;
    typedef const_iterator super;
    iterator(const elt_iterator &i) : super(i) {}
  public:
    iterator() { }
    T *get() const        { return const_cast<T *>(super::get()); }
    T &operator*() const  { return const_cast<T &>(super::operator*()); }
    T *operator->() const { return const_cast<T *>(super::operator->()); }
  };

  iterator begin()             { return elt_iterator(this); }
  const_iterator begin() const { return elt_iterator(this); }

  /// end().live() == false // do we even need end()?
  iterator end()             { return elt_iterator(this, -1, 0, 0); }
  const_iterator end() const { return elt_iterator(this, -1, 0, 0); }

  /// returns end() if none find
  iterator find(const key_type &key)             { return elt_find(key); }
  const_iterator find(const key_type &key) const { return elt_find(key); }

  /// returns iterator to newly inserted element at key
  iterator find_insert(const key_type &key) {
    elt_iterator i = elt_find(key);
    if (!i.live()) elt_set(i, new elt_t(T(key)));
    return i;
  }

  /// returns newly inserted value if not found
  T &operator[](const key_type & key) { return *find_insert(key); }
  /// returns T() if not found
  const T &operator[](const key_type & key) const {
    const_iterator i = find(key);
    return i.live() ? *i : _default;
  }

  /// returns iterator at next element or end()
  iterator erase(const iterator &it) {
    if (!it.live()) return it;
    iterator i(it);
    if (elt_t *e = elt_set(i._rep, 0)) delete e;
    return i;
  }
  /// returns number of erased elements, 0 or 1
  size_t erase(const key_type &key) {
    iterator it = find(key);
    if (it.live()) { erase(it); return 1; }
    return 0;
  }

  void clear() {
    for (elt_iterator it(this); it.live(); ) delete elt_set(it, 0);
  }
};

#if 0
void swap(HashTable<T> &a, HashTable<T> &b) {
  swap(a._buckets, b._buckets);
  swap(a._nbuckets, b._nbuckets);
  swap(a._first_bucket, b._first_bucket);
  swap(a._size, b._size);
}
#endif

#endif // INCLUDED_HASHTABLE_HH

