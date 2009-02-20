// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "unit_tests.hh"
#include "basic_io.hh"

UNIT_TEST(binary_transparency)
{
  std::string testpattern;
  for (unsigned i=0; i<256; ++i) testpattern+=char(i);

  static symbol test("test");

  basic_io::printer printer;
  basic_io::stanza st;
  st.push_str_pair(test, testpattern);
  printer.print_stanza(st);

  basic_io::input_source source(printer.buf, "unit test string");
  basic_io::tokenizer tokenizer(source);
  basic_io::parser parser(tokenizer);
  std::string t1;
  parser.esym(test);
  parser.str(t1);
  I(testpattern==t1);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
