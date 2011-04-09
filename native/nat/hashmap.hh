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

#ifndef INCLUDED_HASHMAP_HH
#define INCLUDED_HASHMAP_HH

#include "hashtable.hh"

/**
 * Pair container that used in HashTable makes a HashMap.
 * NOTE: this is not a general pair, don't use outside of HashTable context
 */
template <class T, class U>
class KV {
  const T _key; // note, we should restrict access to the key
public:
  typedef T key_type;
  typedef U value_type;

  U value;

  KV() : _key(), value() { }
  KV(const T &t, const U &u = U()) : _key(t), value(u) {}
  const T &key() const { return _key; }
  //KV &operator =(const U &u) { value = u; return *this; }
  //operator U() const { return value; }
};

template <typename K, typename V>
class HashMap : public HashTable< KV<K,V> > {
  typedef HashTable< KV<K,V> > super;
  typedef struct super::elt_t elt_t;
public:
  HashMap(size_t n = 63) : super(n) {}
  V &operator[](const K & key) { return find_insert(key)->value; }
  const V &operator[](const K & key) const {
    return get(key);
  }
  const V &get(const K & key) const {
    typename super::const_iterator i = find(key);
    return i.live() ? i->value : super::_default.value;
  }
  V set(const K &key, const V &value) {
    typename super::elt_iterator i = elt_find(key);
    if (i.live()) {
      elt_t *el = i.get();
      V v = el->v.value;
      el->v.value = value;
      return v;
    }
    elt_set(i, new elt_t(KV<K,V>(key, value)));
    return V();
  }
}; // HashMap

#endif // INCLUDED_HASHMAP_HH

