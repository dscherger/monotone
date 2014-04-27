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
#include "../../../src/vocab.hh"

UNIT_TEST(verify_hexenc_id)
{
  // -------- magic empty string and default constructor are okay:
  UNIT_TEST_CHECK(hexenc<id>("")() == "");
  hexenc<id> my_default_id;
  UNIT_TEST_CHECK(my_default_id() == "");

  // -------- wrong length:
  UNIT_TEST_CHECK_THROW(hexenc<id>("a", origin::user), recoverable_failure);
  // 39 letters
  UNIT_TEST_CHECK_THROW(hexenc<id>("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", origin::user),
                    recoverable_failure);
  // 41 letters
  UNIT_TEST_CHECK_THROW(hexenc<id>("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", origin::user),
                    recoverable_failure);
  // but 40 is okay
  UNIT_TEST_CHECK(hexenc<id>("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")()
              == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

  // -------- bad characters:
  UNIT_TEST_CHECK_THROW(hexenc<id>("g000000000000000000000000000000000000000"), unrecoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("g000000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("h000000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("G000000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("H000000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("*000000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("`000000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("z000000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("Z000000000000000000000000000000000000000", origin::user), recoverable_failure);
  // different positions:
  UNIT_TEST_CHECK_THROW(hexenc<id>("g000000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("0g00000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("00g0000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("000g000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("0000g00000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("000000000000000000000g000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("0000000000000000000000g00000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("000000000000000000000000000000g000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("000000000000000000000000000000000000g000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("0000000000000000000000000000000000000g00", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("00000000000000000000000000000000000000g0", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("000000000000000000000000000000000000000g", origin::user), recoverable_failure);
  // uppercase hex is bad too!
  UNIT_TEST_CHECK_THROW(hexenc<id>("A000000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("B000000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("C000000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("D000000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("E000000000000000000000000000000000000000", origin::user), recoverable_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("F000000000000000000000000000000000000000", origin::user), recoverable_failure);
  // but lowercase and digits are all fine
  UNIT_TEST_CHECK(hexenc<id>("0123456789abcdef0123456789abcdef01234567")()
              == "0123456789abcdef0123456789abcdef01234567");
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
