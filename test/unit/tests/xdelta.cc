// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../../../src/base.hh"

// <boost/math/special_functions/detail/lgamma_small.hpp> uses L().
// This conflicts with a #define in "../../../src/sanity.hh".
// Workaround: Include BOOST header before "../../../src/xdelta.hh".
#include <boost/random.hpp>

#include "../unit_tests.hh"
#include "../../../src/xdelta.hh"

#include "../../../src/adler32.hh"

boost::mt19937 xdelta_prng;
boost::uniform_smallint<char> xdelta_chargen('a', 'z');
boost::uniform_smallint<size_t> xdelta_sizegen(1024, 65536);
boost::uniform_smallint<size_t> xdelta_editgen(3, 10);
boost::uniform_smallint<size_t> xdelta_lengen(1, 256);

using std::string;
using boost::shared_ptr;

UNIT_TEST(basic)
{
  data dat1(string("the first day of spring\nmakes me want to sing\n"),
            origin::internal);
  data dat2(string("the first day of summer\nis a major bummer\n"),
            origin::internal);
  delta del;
  diff(dat1, dat2, del);

  data dat3;
  patch(dat1, del, dat3);
  UNIT_TEST_CHECK(dat3 == dat2);
}

static string
apply_via_normal(string const & base, string const & delta)
{
  string tmp;
  apply_delta(base, delta, tmp);
  return tmp;
}

static string
apply_via_piecewise(string const & base, string const & delta)
{
  shared_ptr<delta_applicator> appl = new_piecewise_applicator();
  appl->begin(base);
  apply_delta(appl, delta);
  appl->next();
  string tmp;
  appl->finish(tmp);
  return tmp;
}

static void
spin(string a, string b)
{
  string ab, ba;
  compute_delta(a, b, ab);
  compute_delta(b, a, ba);
  UNIT_TEST_CHECK(a == apply_via_normal(b, ba));
  UNIT_TEST_CHECK(a == apply_via_piecewise(b, ba));
  UNIT_TEST_CHECK(b == apply_via_normal(a, ab));
  UNIT_TEST_CHECK(b == apply_via_piecewise(a, ab));
  string ab_inverted, ba_inverted;
  invert_xdelta(a, ab, ab_inverted);
  invert_xdelta(b, ba, ba_inverted);
  UNIT_TEST_CHECK(a == apply_via_normal(b, ab_inverted));
  UNIT_TEST_CHECK(a == apply_via_piecewise(b, ab_inverted));
  UNIT_TEST_CHECK(b == apply_via_normal(a, ba_inverted));
  UNIT_TEST_CHECK(b == apply_via_piecewise(a, ba_inverted));
}

UNIT_TEST(simple_cases)
{
  L(FL("empty/empty"));
  spin("", "");
  L(FL("empty/short"));
  spin("", "a");
  L(FL("empty/longer"));
  spin("", "asdfasdf");
  L(FL("two identical strings"));
  spin("same string", "same string");
}

void
xdelta_random_string(string & str)
{
  size_t sz = xdelta_sizegen(xdelta_prng);
  str.clear();
  str.reserve(sz);
  while (sz-- > 0)
    {
      str += xdelta_chargen(xdelta_prng);
    }
}

void
xdelta_randomly_insert(string & str)
{
  size_t nedits = xdelta_editgen(xdelta_prng);
  while (nedits > 0)
    {
      size_t pos = xdelta_sizegen(xdelta_prng) % str.size();
      size_t len = xdelta_lengen(xdelta_prng);
      if (pos+len >= str.size())
        continue;
      string tmp;
      tmp.reserve(len);
      for (size_t i = 0; i < len; ++i)
        tmp += xdelta_chargen(xdelta_prng);
        str.insert(pos, tmp);
      nedits--;
    }
}

void
xdelta_randomly_change(string & str)
{
  size_t nedits = xdelta_editgen(xdelta_prng);
  while (nedits > 0)
    {
      size_t pos = xdelta_sizegen(xdelta_prng) % str.size();
      size_t len = xdelta_lengen(xdelta_prng);
      if (pos+len >= str.size())
        continue;
      for (size_t i = 0; i < len; ++i)
        str[pos+i] = xdelta_chargen(xdelta_prng);
      nedits--;
    }
}

void
xdelta_randomly_delete(string & str)
{
  size_t nedits = xdelta_editgen(xdelta_prng);
  while (nedits > 0)
    {
      size_t pos = xdelta_sizegen(xdelta_prng) % str.size();
      size_t len = xdelta_lengen(xdelta_prng);
      if (pos+len >= str.size())
        continue;
      str.erase(pos, len);
      --nedits;
    }
}

UNIT_TEST(random_simple_delta)
{
  for (int i = 0; i < 100; ++i)
    {
      string a, b;
      xdelta_random_string(a);
      b = a;
      xdelta_randomly_change(b);
      xdelta_randomly_insert(b);
      xdelta_randomly_delete(b);
      spin(a, b);
    }
}

UNIT_TEST(random_piecewise_delta)
{
  for (int i = 0; i < 50; ++i)
    {
      string prev, next, got;
      xdelta_random_string(prev);
      shared_ptr<delta_applicator> appl = new_piecewise_applicator();
      appl->begin(prev);
      for (int j = 0; j < 5; ++j)
        {
          appl->finish(got);
          UNIT_TEST_CHECK(got == prev);
          next = prev;
          xdelta_randomly_change(next);
          xdelta_randomly_insert(next);
          xdelta_randomly_delete(next);
          string delta;
          compute_delta(prev, next, delta);
          apply_delta(appl, delta);
          appl->next();
          prev = next;
        }
      appl->finish(got);
      UNIT_TEST_CHECK(got == prev);
  }
}

UNIT_TEST(rolling_sanity_check)
{
  const unsigned testbufsize = 512;
  static const string::size_type blocksz = 64;
  char testbuf[testbufsize];

  for(unsigned i = 0; i < testbufsize; ++i)
    {
      testbuf[i] = xdelta_chargen(xdelta_prng);
    }
  for(unsigned advanceby = 0; advanceby < testbufsize; ++advanceby)
    {
      adler32 incremental(reinterpret_cast<u8 const *>(testbuf), blocksz);
      for(unsigned i = 0; i < advanceby; ++i)
        {
          incremental.out(static_cast<u8>(testbuf[i]));
          if ((i + blocksz) < testbufsize)
            {
              incremental.in(static_cast<u8>(testbuf[i+blocksz]));
            }
        }
      adler32 skip(reinterpret_cast<u8 const *>(testbuf), blocksz);
      u32 new_lo = advanceby;
      u32 new_hi = new_lo + blocksz;
      if (new_hi > testbufsize)
        {
          new_hi = testbufsize;
        }
      skip.replace_with(reinterpret_cast<u8 const *>(testbuf + new_lo), new_hi - new_lo);

      UNIT_TEST_CHECK(skip.sum() == incremental.sum());
    }
  L(FL("rolling sanity check passed"));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
