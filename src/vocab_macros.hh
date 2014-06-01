// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

//HH

#define hh_ENCODING(enc)                               \
                                                       \
template<typename INNER>                               \
class enc;                                             \
                                                       \
template <typename INNER>                              \
std::ostream & operator<<(std::ostream &,              \
                          enc<INNER> const &);         \
                                                       \
template <typename INNER>                              \
void dump(enc<INNER> const &, std::string &);          \
                                                       \
template<typename INNER>                               \
class enc : public origin_aware {                      \
  immutable_string s;                                  \
public:                                                \
  enc() {}                                             \
  explicit enc(char const * const s);                  \
  enc(std::string const & s, origin::type m);          \
  enc(std::string && s, origin::type m);               \
  enc(enc<INNER> const & other);                       \
  enc(enc<INNER> && other);                            \
  enc<INNER> const &                                   \
  operator=(enc<INNER> const & other);                 \
  enc<INNER> const &                                   \
  operator=(enc<INNER> && other);                      \
  std::string const & operator()() const               \
    { return s.get(); }                                \
  bool operator<(enc<INNER> const & x) const           \
    { return s.get() < x.s.get(); }                    \
  bool operator==(enc<INNER> const & x) const          \
    { return s.get() == x.s.get(); }                   \
  bool operator!=(enc<INNER> const & x) const          \
    { return s.get() != x.s.get(); }                   \
  friend std::ostream & operator<< <>(std::ostream &,  \
                                 enc<INNER> const &);  \
};

#define hh_ENCODING_NOVERIFY(enc) hh_ENCODING(enc)


#define hh_DECORATE(dec)                               \
                                                       \
template<typename INNER>                               \
class dec;                                             \
                                                       \
template <typename INNER>                              \
std::ostream & operator<<(std::ostream &,              \
                          dec<INNER> const &);         \
                                                       \
template <typename INNER>                              \
void dump(dec<INNER> const &, std::string &);          \
                                                       \
template<typename INNER>                               \
class dec {                                            \
  INNER i;                                             \
public:                                                \
  dec() {}                                             \
  explicit dec(char const * const s);                  \
  dec(std::string const & s, origin::type m);          \
  dec(std::string && s, origin::type m);               \
  explicit dec(INNER const & inner);                   \
  explicit dec(INNER && inner);                        \
  dec(dec<INNER> const & other);                       \
  dec(dec<INNER> && other);                            \
  bool operator<(dec<INNER> const & x) const           \
    { return i < x.i; }                                \
  INNER const & inner() const                          \
    { return i; }                                      \
  dec<INNER> const &                                   \
  operator=(dec<INNER> const & other);                 \
  dec<INNER> const &                                   \
  operator=(dec<INNER> && other);                      \
  bool operator==(dec<INNER> const & x) const          \
    { return i == x.i; }                               \
  bool operator!=(dec<INNER> const & x) const          \
    { return !(i == x.i); }                            \
  friend std::ostream & operator<< <>(std::ostream &,  \
                                 dec<INNER> const &);  \
};


#define hh_ATOMIC_HOOKED(ty, hook)                     \
class ty;                                              \
                                                       \
std::ostream & operator<<(std::ostream &, ty const &); \
                                                       \
template <>                                            \
void dump(ty const &, std::string &);                  \
                                                       \
class ty : public origin_aware {                       \
  immutable_string s;                                  \
public:                                                \
  ty() {}                                              \
  explicit ty(char const * const str);                 \
  ty(std::string const & str, origin::type m);         \
  ty(std::string && str, origin::type m);              \
  ty(ty const & other);                                \
  ty(ty && other);                                     \
  ty const & operator=(ty const & other);              \
  ty const & operator=(ty && other);                   \
  std::string const & operator()() const               \
    { return s.get(); }                                \
  bool operator<(ty const & x) const                   \
    { return s.get() < x.s.get(); }                    \
  bool operator==(ty const & x) const                  \
    { return s.get() == x.s.get(); }                   \
  bool operator!=(ty const & x) const                  \
    { return s.get() != x.s.get(); }                   \
  friend std::ostream & operator<<(std::ostream &,     \
                                   ty const &);        \
  hook                                                 \
  struct symtab                                        \
  {                                                    \
    symtab();                                          \
    ~symtab();                                         \
  };                                                   \
};

#define hh_ATOMIC(ty) hh_ATOMIC_HOOKED(ty,)
#define hh_ATOMIC_NOVERIFY(ty) hh_ATOMIC(ty)
#define hh_ATOMIC_BINARY(ty) hh_ATOMIC(ty)

//CC


#define cc_ATOMIC(ty)                        \
                                             \
static symtab_impl ty ## _tab;               \
static size_t ty ## _tab_active = 0;         \
                                             \
ty::ty(char const * const str) :             \
  origin_aware(),                            \
  s((ty ## _tab_active > 0)                  \
    ? (ty ## _tab.unique(str))               \
    : str)                                   \
{ verify(*this); }                           \
                                             \
ty::ty(string const & str,                   \
       origin::type m) :                     \
  origin_aware(m),                           \
  s((ty ## _tab_active > 0)                  \
    ? (ty ## _tab.unique(str))               \
    : str)                                   \
{ verify(*this); }                           \
                                             \
ty::ty(string && str,                        \
       origin::type m) :                     \
  origin_aware(m),                           \
  s((ty ## _tab_active > 0)                  \
    ? (ty ## _tab.unique(move(str)))         \
    : move(str))                             \
{ verify(*this); }                           \
                                             \
ty::ty(ty const & other) :                   \
  origin_aware(other), s(other.s) {}         \
                                             \
ty::ty(ty && other) :                        \
  origin_aware(other),                       \
  s(move(other.s)) {}                        \
                                             \
ty const & ty::operator=(ty const & other)   \
{                                            \
  s = other.s;                               \
  made_from = other.made_from;               \
  return *this;                              \
}                                            \
                                             \
ty const & ty::operator=(ty && other)        \
{                                            \
  if (this != &other)                        \
    {                                        \
      swap(s, other.s);                      \
      made_from = other.made_from;           \
    }                                        \
  return *this;                              \
}                                            \
                                             \
std::ostream & operator<<(std::ostream & o,  \
                          ty const & a)      \
{ return (o << a.s.get()); }                 \
                                             \
template <>                                  \
void dump(ty const & obj, std::string & out) \
{ out = obj(); }                             \
                                             \
ty::symtab::symtab()                         \
{ ty ## _tab_active++; }                     \
                                             \
ty::symtab::~symtab()                        \
{                                            \
  I(ty ## _tab_active > 0);                  \
  ty ## _tab_active--;                       \
  if (ty ## _tab_active == 0)                \
    ty ## _tab.clear();                      \
}


#define cc_ATOMIC_NOVERIFY(ty)               \
inline void verify(ty const &) {}            \
cc_ATOMIC(ty)


#define cc_ATOMIC_BINARY(ty)                 \
                                             \
static symtab_impl ty ## _tab;               \
static size_t ty ## _tab_active = 0;         \
                                             \
ty::ty(char const * const str) :             \
  origin_aware(),                            \
  s((ty ## _tab_active > 0)                  \
    ? (ty ## _tab.unique(str))               \
    : str)                                   \
{ verify(*this); }                           \
                                             \
ty::ty(string const & str,                   \
       origin::type m) :                     \
  origin_aware(m),                           \
  s((ty ## _tab_active > 0)                  \
    ? (ty ## _tab.unique(str))               \
    : str)                                   \
{ verify(*this); }                           \
                                             \
ty::ty(string && str,                        \
       origin::type m) :                     \
  origin_aware(m),                           \
  s((ty ## _tab_active > 0)                  \
    ? (ty ## _tab.unique(move(str)))         \
    : move(str))                             \
{ verify(*this); }                           \
                                             \
ty::ty(ty const & other) :                   \
  origin_aware(other), s(other.s) {}         \
                                             \
ty::ty(ty && other) :                        \
  origin_aware(other), s(move(other.s)) {}   \
                                             \
ty const & ty::operator=(ty const & other)   \
{                                            \
  s = other.s;                               \
  made_from = other.made_from;               \
  return *this;                              \
}                                            \
                                             \
ty const & ty::operator=(ty && other)        \
{                                            \
  if (this != &other)                        \
    {                                        \
      swap(s, other.s);                      \
      made_from = other.made_from;           \
    }                                        \
  return *this;                              \
}                                            \
                                             \
std::ostream & operator<<(std::ostream & o,  \
                          ty const & a)      \
{ return (o << encode_hexenc(a(), a.made_from)); }      \
                                             \
ty::symtab::symtab()                         \
{ ty ## _tab_active++; }                     \
                                             \
ty::symtab::~symtab()                        \
{                                            \
  I(ty ## _tab_active > 0);                  \
  ty ## _tab_active--;                       \
  if (ty ## _tab_active == 0)                \
    ty ## _tab.clear();                      \
}



#define cc_ENCODING(enc)                                 \
                                                         \
template<typename INNER>                                 \
enc<INNER>::enc(char const * const s) :                  \
  origin_aware(), s(s)                                   \
  { verify(*this); }                                     \
                                                         \
template<typename INNER>                                 \
enc<INNER>::enc(string const & s, origin::type m) :      \
  origin_aware(m), s(s)                                  \
  { verify(*this); }                                     \
                                                         \
template<typename INNER>                                 \
enc<INNER>::enc(string && s, origin::type m) :           \
  origin_aware(m), s(move(s))                            \
  { verify(*this); }                                     \
                                                         \
template<typename INNER>                                 \
enc<INNER>::enc(enc<INNER> const & other)                \
  : origin_aware(other), s(other.s) {}                   \
                                                         \
template<typename INNER>                                 \
enc<INNER>::enc(enc<INNER> && other)                     \
  : origin_aware(other), s(move(other.s)) {}             \
                                                         \
template<typename INNER>                                 \
enc<INNER> const &                                       \
enc<INNER>::operator=(enc<INNER> const & other)          \
{                                                        \
  s = other.s;                                           \
  made_from = other.made_from;                           \
  return *this;                                          \
}                                                        \
                                                         \
template<typename INNER>                                 \
enc<INNER> const &                                       \
enc<INNER>::operator=(enc<INNER> && other)               \
{                                                        \
  if (this != &other)                                    \
    {                                                    \
      swap(s, other.s);                                  \
      made_from = other.made_from;                       \
    }                                                    \
  return *this;                                          \
}                                                        \
                                                         \
template <typename INNER>                                \
std::ostream & operator<<(std::ostream & o,              \
                          enc<INNER> const & e)          \
{ return (o << e.s.get()); }                             \
                                                         \
template <typename INNER>                                \
void dump(enc<INNER> const & obj, std::string & out)     \
{ out = obj(); }

#define cc_ENCODING_NOVERIFY(enc)               \
template<typename INNER>                        \
inline void verify(enc<INNER> const &) {}       \
cc_ENCODING(enc)



#define cc_DECORATE(dec)                                 \
                                                         \
template<typename INNER>                                 \
dec<INNER>::dec(dec<INNER> const & other)                \
  : i(other.i) {}                                        \
                                                         \
template<typename INNER>                                 \
dec<INNER>::dec(dec<INNER> && other)                     \
  : i(move(other.i)) {}                                  \
                                                         \
template<typename INNER>                                 \
dec<INNER>::dec(char const * const s)                    \
  : i(s) { verify(i); }                                  \
                                                         \
template<typename INNER>                                 \
dec<INNER>::dec(std::string const & s,                   \
                origin::type m)                          \
  : i(s, m) { verify(i); }                               \
                                                         \
template<typename INNER>                                 \
dec<INNER>::dec(std::string && s,                        \
                origin::type m)                          \
  : i(move(s), m) { verify(i); }                         \
                                                         \
template<typename INNER>                                 \
dec<INNER>::dec(INNER const & inner)                     \
  : i(inner) {}                                          \
                                                         \
template<typename INNER>                                 \
dec<INNER>::dec(INNER && inner)                          \
  : i(move(inner)) {}                                    \
                                                         \
template<typename INNER>                                 \
dec<INNER> const &                                       \
dec<INNER>::operator=(dec<INNER> const & other)          \
  { i = other.i; return *this; }                         \
                                                         \
template<typename INNER>                                 \
dec<INNER> const &                                       \
dec<INNER>::operator=(dec<INNER> && other)               \
  {                                                      \
    if (this != &other)                                  \
      swap(i, other.i);                                  \
    return *this;                                        \
  }                                                      \
                                                         \
template <typename INNER>                                \
std::ostream & operator<<(std::ostream & o,              \
                          dec<INNER> const & d)          \
{ return (o << d.i); }                                   \
                                                         \
template <typename INNER>                                \
void dump(dec<INNER> const & obj, std::string & out)     \
{ dump(obj.inner(), out); }

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
