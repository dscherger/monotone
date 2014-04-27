// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../../../src/base.hh"
#include "../unit_tests.hh"
#include "../../../src/transforms.hh"

using std::string;

UNIT_TEST(enc)
{
  data d2, d1("the rain in spain");
  gzip<data> gzd1, gzd2;
  base64< gzip<data> > bgzd;
  encode_gzip(d1, gzd1);
  bgzd = encode_base64(gzd1);
  gzd2 = decode_base64(bgzd);
  UNIT_TEST_CHECK(gzd2 == gzd1);
  decode_gzip(gzd2, d2);
  UNIT_TEST_CHECK(d2 == d1);
}

UNIT_TEST(calculate_ident)
{
  data input(string("the only blender which can be turned into the most powerful vaccum cleaner"),
             origin::internal);
  id output;
  string ident("86e03bdb3870e2a207dfd0dcbfd4c4f2e3bc97bd");
  calculate_ident(input, output);
  UNIT_TEST_CHECK(output() == decode_hexenc(ident, origin::internal));
}

UNIT_TEST(corruption_check)
{
  data input(string("i'm so fragile, fragile when you're here"), origin::internal);
  gzip<data> gzd;
  encode_gzip(input, gzd);

  // fake a single-bit error
  string gzs = gzd();
  string::iterator i = gzs.begin();
  while (*i != '+')
    i++;
  *i = 'k';

  gzip<data> gzbad(gzs, origin::network);
  data output;
  UNIT_TEST_CHECK_THROW(decode_gzip(gzbad, output), recoverable_failure);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
