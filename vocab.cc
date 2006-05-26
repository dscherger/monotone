// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <iostream>

#include "constants.hh"
#include "hash_map.hh"
#include "sanity.hh"
#include "vocab.hh"

// verifiers for various types of data

using namespace std;

// the verify() stuff gets a little complicated; there doesn't seem to be a
// really nice way to achieve what we want with c++'s type system.  the
// problem is this: we want to give verify(file_path) and verify(local_path)
// access to the internals of file_path and local_path, i.e. make them
// friends, so they can normalize the file paths they're given.  this means
// that verify() needs to be declared publically, so that the definition of
// these classes can refer to them.  it also means that they -- and all other
// ATOMIC types -- cannot fall back on a templated version of verify if no
// other version is defined, because, well, the friend thing and the template
// thing just don't work out, as far as I can tell.  So, every ATOMIC type
// needs an explicitly defined verify() function, so we have both ATOMIC() and
// ATOMIC_NOVERIFY() macros, the latter of which defines a type-specific noop
// verify function.  DECORATE and ENCODING, on the other hand, cannot make use
// of a trick like these, because they are template types themselves, and we
// want to be able to define verify(hexenc<id>) without defining
// verify(hexenc<data>) at the same time, for instance.  Fortunately, these
// types never need to be friends with their verify functions (yet...), so we
// _can_ use a templated fallback function.  This templated function is used
// _only_ by DECORATE and ENCODING; it would be nice to make it take an
// argument of type T1<T2> to document that, but for some reason that doesn't
// work either.
template <typename T>
static inline void
verify(T & val)
{}

inline void 
verify(path_component & val)
{
  // FIXME: probably ought to do something here?
  val.ok = true;
}

inline void 
verify(hexenc<id> & val)
{
  if (val.ok)
    return;

  if (val().empty())
    return;

  N(val().size() == constants::idlen,
    F("hex encoded ID '%s' size != %d") % val % constants::idlen);
  for (string::const_iterator i = val().begin(); i != val().end(); ++i)
    {
      N(is_xdigit(*i),
	F("bad character '%c' in id name '%s'") % *i % val);
    }
  val.ok = true;
}

inline void 
verify(ace & val)
{
  if (val.ok)
    return;

  string::size_type pos = val().find_first_not_of(constants::legal_ace_bytes);
  N(pos == string::npos,
    F("bad character '%c' in ace string '%s'") % val().at(pos) % val);

  val.ok = true;
}

inline void
verify(symbol & val)
{
  if (val.ok)
    return;

  for (string::const_iterator i = val().begin(); i != val().end(); ++i)
    {
      N(is_alnum(*i) || *i == '_',
	F("bad character '%c' in symbol '%s'") % *i % val);
    }

  val.ok = true;
}

inline void 
verify(cert_name & val)
{
  if (val.ok)
    return;

  string::size_type pos = val().find_first_not_of(constants::legal_cert_name_bytes);
  N(pos == string::npos,
    F("bad character '%c' in cert name '%s'") % val().at(pos) % val);

  val.ok = true;
}

inline void 
verify(rsa_keypair_id & val)
{
  if (val.ok)
    return;

  string::size_type pos = val().find_first_not_of(constants::legal_key_name_bytes);
  N(pos == string::npos,
    F("bad character '%c' in key name '%s'") % val().at(pos) % val);

  val.ok = true;
}

inline void
verify(netsync_session_key & val)
{
  if (val.ok)
    return;

  if (val().size() == 0)
    {
      val.s.append(constants::netsync_session_key_length_in_bytes, 0);
      return;
    }

  N(val().size() == constants::netsync_session_key_length_in_bytes,
    F("Invalid key length of %d bytes") % val().length());

  val.ok = true;
}

inline void
verify(netsync_hmac_value & val)
{
  if (val.ok)
    return;

  if (val().size() == 0)
    {
      val.s.append(constants::netsync_hmac_value_length_in_bytes, 0);
      return;
    }

  N(val().size() == constants::netsync_hmac_value_length_in_bytes,
    F("Invalid hmac length of %d bytes") % val().length());

  val.ok = true;
}


// Note that ATOMIC types each keep a static symbol-table object and a
// counter of activations, and when there is an activation, the
// members of the ATOMIC type initialize their internal string using a
// copy of the string found in the symtab. Since some (all?) C++
// std::string implementations are copy-on-write, this has the affect
// of making the ATOMIC(foo) values constructed within a symbol table
// scope share string storage.
struct 
symtab_impl 
{
  typedef hashmap::hash_set<std::string> hset;
  hset vals;
  symtab_impl() : vals() {}
  void clear() { vals.clear(); }
  std::string const & unique(std::string const & in) 
  {
    // This produces a pair <iter,bool> where iter points to an
    // element of the table; the bool indicates whether the element is
    // new, but we don't actually care. We just want the iter.
    return *(vals.insert(in).first);
  }
};


// instantiation of various vocab functions



#include "vocab_macros.hh"
#define ENCODING(enc) cc_ENCODING(enc)
#define DECORATE(dec) cc_DECORATE(dec)
#define ATOMIC(ty) cc_ATOMIC(ty)
#define ATOMIC_NOVERIFY(ty) cc_ATOMIC_NOVERIFY(ty)

#define EXTERN 

#include "vocab_terms.hh"

#undef EXTERN
#undef ATOMIC
#undef DECORATE


template
void dump<rsa_pub_key>(base64<rsa_pub_key> const&, std::string &);

template
void dump(revision_id const & r, std::string &);

template
void dump(roster_id const & r, std::string &);

template
void dump(manifest_id const & r, std::string &);

template
void dump(file_id const & r, std::string &);

template
void dump(hexenc<id> const & r, std::string &);

// the rest is unit tests

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

void add_vocab_tests(test_suite * suite)
{
  I(suite);
  // None, ATM.
}

#endif // BUILD_UNIT_TESTS
