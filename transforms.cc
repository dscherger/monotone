// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <algorithm>
#include <cctype>
#include <functional>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <wchar.h>

#include <boost/tokenizer.hpp>
#include <boost/scoped_array.hpp>

#include "botan/botan.h"
#include "botan/gzip.h"
#include "botan/sha160.h"

#include "cleanup.hh"
#include "constants.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "simplestring_xform.hh"
#include "vocab.hh"
#include "xdelta.hh"

using namespace std;

// this file contans various sorts of string transformations. each
// transformation should be self-explanatory from its type signature. see
// transforms.hh for the summary.

// NB this file uses very "value-centric" functional approach; even though
// many of the underlying transformations are "stream-centric" and the
// underlying libraries (eg. crypto++) are stream oriented. this will
// probably strike some people as contemptably inefficient, since it means
// that occasionally 1, 2, or even 3 copies of an entire file will wind up
// in memory at once. I am taking this approach for 3 reasons: first, I
// want the type system to help me and value types are much easier to work
// with than stream types. second, it is *much* easier to debug a program
// that operates on values than streams, and correctness takes precedence
// over all other features of this program. third, this is a peer-to-peer
// sort of program for small-ish source-code text files, not a fileserver,
// and is memory-limited anyways (for example, storing things in sqlite
// requires they be able to fit in memory). you're hopefully not going to
// be dealing with hundreds of users hammering on locks and memory
// concurrently.
//
// if future analysis proves these assumptions wrong, feel free to revisit
// the matter, but bring strong evidence along with you that the stream
// paradigm "must" be used. this program is intended for source code
// control and I make no bones about it.

using namespace std;

// the generic function
template<typename XFM> string xform(string const & in)
{
  string out;
  Botan::Pipe pipe(new XFM());
  pipe.process_msg(in);
  out = pipe.read_all_as_string();
  return out;
}

// specialize it
template string xform<Botan::Base64_Encoder>(string const &);
template string xform<Botan::Base64_Decoder>(string const &);
template string xform<Botan::Hex_Encoder>(string const &);
template string xform<Botan::Hex_Decoder>(string const &);
template string xform<Botan::Gzip_Compression>(string const &);
template string xform<Botan::Gzip_Decompression>(string const &);

// for use in hexenc encoding

static inline void
encode_hexenc_inner(string::const_iterator i,
                    string::const_iterator end,
                    char *out)
{
  static char const *tab = "0123456789abcdef";
  for (; i != end; ++i)
    {
      *out++ = tab[(*i >> 4) & 0xf];
      *out++ = tab[*i & 0xf];
    }
}
                                  

string encode_hexenc(string const & in)
{
  if (LIKELY(in.size() == constants::idlen / 2))
    {
      char buf[constants::idlen];
      encode_hexenc_inner(in.begin(), in.end(), buf);
      return string(buf, constants::idlen);
    }
  else
    {
      boost::scoped_array<char> buf(new char[in.size() * 2]);
      encode_hexenc_inner(in.begin(), in.end(), buf.get());
      return string(buf.get(), in.size() *2);
    }
}

static inline char 
decode_hex_char(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  I(false);
}

static inline void
decode_hexenc_inner(string::const_iterator i,
                    string::const_iterator end,
                    char *out)
{
  for (; i != end; ++i)
    {
      char t = decode_hex_char(*i++);
      t <<= 4;
      t |= decode_hex_char(*i);
      *out++ = t;
    }
}

string decode_hexenc(string const & in)
{
  
  I(in.size() % 2 == 0);
  if (LIKELY(in.size() == constants::idlen))
    {
      char buf[constants::idlen / 2];
      decode_hexenc_inner(in.begin(), in.end(), buf);
      return string(buf, constants::idlen / 2);
    }
  else 
    {
      boost::scoped_array<char> buf(new char[in.size() / 2]);
      decode_hexenc_inner(in.begin(), in.end(), buf.get());
      return string(buf.get(), in.size() / 2);
    }
}

template <typename T>
void pack(T const & in, base64< gzip<T> > & out)
{
  string tmp;
  tmp.reserve(in().size()); // FIXME: do some benchmarking and make this a constant::

  Botan::Pipe pipe(new Botan::Gzip_Compression(), new Botan::Base64_Encoder);
  pipe.process_msg(in());
  tmp = pipe.read_all_as_string();
  out = tmp;
}

template <typename T>
void unpack(base64< gzip<T> > const & in, T & out)
{
  string tmp;
  tmp.reserve(in().size()); // FIXME: do some benchmarking and make this a constant::

  Botan::Pipe pipe(new Botan::Base64_Decoder(), new Botan::Gzip_Decompression());
  pipe.process_msg(in());
  tmp = pipe.read_all_as_string();

  out = tmp;
}

// specialise them
template void pack<data>(data const &, base64< gzip<data> > &);
template void pack<delta>(delta const &, base64< gzip<delta> > &);
template void unpack<data>(base64< gzip<data> > const &, data &);
template void unpack<delta>(base64< gzip<delta> > const &, delta &);

// diffing and patching

void 
diff(data const & olddata,
     data const & newdata,
     delta & del)
{
  string unpacked;
  compute_delta(olddata(), newdata(), unpacked);
  del = delta(unpacked);
}

void 
patch(data const & olddata,
      delta const & del,
      data & newdata)
{
  string result;
  apply_delta(olddata(), del(), result);
  newdata = result;
}

// identifier (a.k.a. sha1 signature) calculation

void 
calculate_ident(data const & dat,
                hexenc<id> & ident)
{
  Botan::Pipe p(new Botan::Hash_Filter("SHA-160"));
  p.process_msg(dat());

  id ident_decoded(p.read_all_as_string());
  encode_hexenc(ident_decoded, ident);  
}

void 
calculate_ident(base64< gzip<data> > const & dat,
                hexenc<id> & ident)
{
  gzip<data> data_decoded;
  data data_decompressed;  
  decode_base64(dat, data_decoded);
  decode_gzip(data_decoded, data_decompressed);  
  calculate_ident(data_decompressed, ident);
}

void 
calculate_ident(file_data const & dat,
                file_id & ident)
{
  hexenc<id> tmp;
  calculate_ident(dat.inner(), tmp);
  ident = tmp;
}

void calculate_ident(revision_data const & dat,
                     revision_id & ident)
{
  hexenc<id> tmp;
  calculate_ident(dat.inner(), tmp);
  ident = tmp;
}

string 
canonical_base64(string const & s)
{
  return xform<Botan::Base64_Encoder>
    (xform<Botan::Base64_Decoder>(s));
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include <stdlib.h>

static void 
enc_test()
{
  data d2, d1("the rain in spain");
  gzip<data> gzd1, gzd2;
  base64< gzip<data> > bgzd;
  encode_gzip(d1, gzd1);
  encode_base64(gzd1, bgzd);
  decode_base64(bgzd, gzd2);
  BOOST_CHECK(gzd2 == gzd1);
  decode_gzip(gzd2, d2);
  BOOST_CHECK(d2 == d1);
}

static void 
rdiff_test()
{
  data dat1(string("the first day of spring\nmakes me want to sing\n"));
  data dat2(string("the first day of summer\nis a major bummer\n"));
  delta del;
  diff(dat1, dat2, del);
  
  data dat3;
  patch(dat1, del, dat3);
  BOOST_CHECK(dat3 == dat2);
}

static void 
calculate_ident_test()
{
  data input(string("the only blender which can be turned into the most powerful vaccum cleaner"));
  hexenc<id> output;
  string ident("86e03bdb3870e2a207dfd0dcbfd4c4f2e3bc97bd");
  calculate_ident(input, output);
  BOOST_CHECK(output() == ident);
}

void 
add_transform_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&enc_test));
  suite->add(BOOST_TEST_CASE(&rdiff_test));
  suite->add(BOOST_TEST_CASE(&calculate_ident_test));
}

#endif // BUILD_UNIT_TESTS
