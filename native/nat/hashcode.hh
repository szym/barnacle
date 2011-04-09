#ifndef INCLUDED_HASHCODE_HH
#define INCLUDED_HASHCODE_HH

#include <sys/types.h>

typedef size_t hashcode_t;      ///< Typical type for a hashcode() value.

template <typename T>
inline hashcode_t hashcode(const T &x) { return x.hashcode(); }

template<> inline hashcode_t hashcode(const char &x)            { return x; }
template<> inline hashcode_t hashcode(const unsigned char &x)   { return x; }
template<> inline hashcode_t hashcode(const short &x)           { return x; }
template<> inline hashcode_t hashcode(const unsigned short &x)  { return x; }
template<> inline hashcode_t hashcode(const int &x)             { return x; }
template<> inline hashcode_t hashcode(const unsigned &x)        { return x; }
template<> inline hashcode_t hashcode(const long &x)            { return x; }
template<> inline hashcode_t hashcode(const unsigned long &x)   { return x; }

#endif // INCLUDED_HASHCODE_HH
