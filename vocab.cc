// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <boost/filesystem/path.hpp>

#include "constants.hh"
#include "network.hh"
#include "sanity.hh"
#include "url.hh"
#include "vocab.hh"

// verifiers for various types of data

using namespace std;

template <typename T>
static inline void verify(T & val)
{}

template <>
static inline void verify(hexenc<id> & val)
{
  if (val.ok)
    return;

  if (val() == "")
    return;

  N(val().size() == constants::idlen,
    F("hex encoded ID '%s' size != %d") % val % constants::idlen);
  string::size_type pos = val().find_first_not_of(constants::legal_id_bytes);
  N(pos == string::npos,
    F("bad character '%c' in id name '%s'") % val().at(pos) % val);

  val.ok = true;
}

template <>
static inline void verify(ace & val)
{
  if (val.ok)
    return;

  string::size_type pos = val().find_first_not_of(constants::legal_ace_bytes);
  N(pos == string::npos,
    F("bad character '%c' in ace string '%s'") % val().at(pos) % val);

  val.ok = true;
}

template <>
static inline void verify(urlenc & val)
{
  if (val.ok)
    return;

  string::size_type pos = val().find_first_not_of(constants::legal_url_bytes);
  N(pos == string::npos,
    F("bad character '%c' in URL-encoded string '%s'") % val().at(pos) % val);

  val.ok = true;
}

template <>
static inline void verify(cert_name & val)
{
  if (val.ok)
    return;

  string::size_type pos = val().find_first_not_of(constants::legal_cert_name_bytes);
  N(pos == string::npos,
    F("bad character '%c' in cert name '%s'") % val().at(pos) % val);

  val.ok = true;
}

template <>
static inline void verify(rsa_keypair_id & val)
{
  if (val.ok)
    return;

  string::size_type pos = val().find_first_not_of(constants::legal_key_name_bytes);
  N(pos == string::npos,
    F("bad character '%c' in key name '%s'") % val().at(pos) % val);

  val.ok = true;
}


template <>
static inline void verify(url & val)
{
  if (val.ok)
    return;

  if (val() == "")
    return;

  string::size_type pos = val().find_first_not_of(constants::legal_url_bytes);
  N(pos == string::npos,
    F("bad character '%c' in URL name '%s'") % val().at(pos) % val);

  ace user, host, group;
  urlenc path;
  string proto;
  unsigned long port;
  N(parse_url(val, proto, user, host, path, group, port),
    F("malformed URL: '%s'") % val);

  val.ok = true;
}


template<>
static inline void verify(local_path & val)
{

  if (val.ok)
    return;

  using boost::filesystem::path;
  boost::filesystem::path p;
  try 
    {
      p = boost::filesystem::path(val());
    }
  catch (std::runtime_error &e)
    {
      throw informative_failure(e.what());
    }

  N(! (p.has_root_path() || p.has_root_name() || p.has_root_directory()),
    F("prohibited absolute path '%s'") % val);

  for(path::iterator i = p.begin(); i != p.end(); ++i)
    {
      N(!( *i == "" && (! p.empty())),
	F("empty path component in '%s'") % val);

      N(!(*i == "." || *i == ".."),
	F("prohibited path component '%s' in '%s'") % *i % val);

      string::size_type pos = val().find_first_of(constants::illegal_path_bytes);
      N(pos == string::npos,
	F("bad character '%c' in path component '%s' in '%s'") % *i % val);
    }
  
  val.ok = true;
}

// fwd declare..
bool book_keeping_file(local_path const & path);

template<>
static inline void verify(file_path & val)
{
  if (val.ok)
    return;
  
  local_path loc(val());
  verify(loc);
  N(!book_keeping_file(loc),
    F("prohibited book-keeping path in '%s'") % val);
  
  val.ok = true;
}


// instantiation of various vocab functions

#define ATOMIC(ty)                           \
                                             \
ty::ty(string const & str) :                 \
     s(str), ok(false)                       \
{ verify(*this); }                           \
                                             \
ty::ty(ty const & other) :                   \
            s(other.s), ok(other.ok)         \
{ verify(*this); }                           \
                                             \
ty const & ty::operator=(ty const & other)   \
{ s = other.s; ok = other.ok;                \
  verify(*this); return *this; }             \
                                             \
ostream & operator<<(ostream & o,            \
                     ty const & a)           \
{ return (o << a.s); }




#define ENCODING(enc)                                    \
                                                         \
template<typename INNER>                                 \
enc<INNER>::enc(string const & s) : i(s), ok(false)      \
  { verify(*this); }                                     \
                                                         \
template<typename INNER>                                 \
enc<INNER>::enc(enc<INNER> const & other)                \
  : i(other.i()), ok(other.ok) { verify(*this); }        \
                                                         \
template<typename INNER>                                 \
enc<INNER>::enc(INNER const & inner) :                   \
    i(inner), ok(false)                                  \
  { verify(*this); }                                     \
                                                         \
template<typename INNER>                                 \
enc<INNER> const &                                       \
enc<INNER>::operator=(enc<INNER> const & other)          \
  { i = other.i; ok = other.ok;                          \
    verify(*this); return *this;}                        \
                                                         \
template <typename INNER>                                \
ostream & operator<<(ostream & o, enc<INNER> const & e)  \
{ return (o << e.i); }


#define DECORATE(dec)                                    \
                                                         \
template<typename INNER>                                 \
dec<INNER>::dec(dec<INNER> const & other)                \
  : i(other.i), ok(other.ok) { verify(*this); }          \
                                                         \
template<typename INNER>                                 \
dec<INNER>::dec(INNER const & inner) :                   \
    i(inner), ok(false)                                  \
  { verify(*this); }                                     \
                                                         \
template<typename INNER>                                 \
dec<INNER> const &                                       \
dec<INNER>::operator=(dec<INNER> const & other)          \
  { i = other.i; ok = other.ok;                          \
    verify(*this); return *this;}                        \
                                                         \
template <typename INNER>                                \
ostream & operator<<(ostream & o, dec<INNER> const & d)  \
{ return (o << d.i); }


#define EXTERN 

#include "vocab_terms.hh"

#undef EXTERN
#undef ATOMIC
#undef DECORATE

template class manifest<cert>;
template class file<cert>;

// the rest is unit tests

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

static void test_file_path_verification()
{
  char const * baddies [] = {"./redundant",
			     "../escape",
			     "foo/../../escape",
			     "foo//nonsense",
			     "/rooted",
			     "c:\\windows\\rooted",
			     "c:/windows/rooted",
			     "c:thing",
			     0 };
  
  for (char const ** c = baddies; *c; ++c)
    BOOST_CHECK_THROW(file_path p(*c), informative_failure);      
  
  char const * bad = "\t\r\n\v\f\a\b";
  char badboy[] = "bad";
  for (char const * c = bad; *c; ++c)
    {
      badboy[1] = *c;
      BOOST_CHECK_THROW(file_path p(badboy), informative_failure);
    }
  
  char const * goodies [] = {"unrooted", 
			     "unrooted.txt",
			     "fun_with_underscore.png",
			     "fun-with-hyphen.tiff", 
			     "unrooted/../unescaping",
			     "unrooted/general/path",
			     0 };

  for (char const ** c = goodies; *c; ++c)
    BOOST_CHECK_NOT_THROW(file_path p(*c), informative_failure);
}

void add_vocab_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&test_file_path_verification));
}

#endif // BUILD_UNIT_TESTS
