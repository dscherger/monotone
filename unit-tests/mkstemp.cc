// Copyright (C) 2009 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "unit_tests.hh"
#include "file_io.hh"
#include "sanity.hh"

#include <cerrno>
#include <cstring>

using std::string;
using std::strerror;

UNIT_TEST(basic)
{
  // This test verifies that we can create 100x3 temporary files in the
  // same directory (using 3 different templates) and that the correct
  // part of the template pathname is modified in each case.

  char const * const cases[4] = {
    "a-XXXXXX", "XXXXXX-b", "c-XXXXXX.dat", 0
  };

  for (int i = 0; cases[i]; i++)
    for (int j = 0; j < 100; j++)
      {
        string r(cases[i]);
        string s(cases[i]);
        if (monotone_mkstemp(s))
          {
            UNIT_TEST_CHECK_MSG(r.length() == s.length(),
                                FL("same length: from %s got %s")
                                % r % s);
            bool no_scribble = true;
            for (string::size_type n = 0; n < r.length(); n++)
              {
                bool ok = r[n] == s[n];
                if (r[n] == 'X')
                  ok = !ok;
                if (!ok)
                  no_scribble = false;
              }
            UNIT_TEST_CHECK_MSG(no_scribble,
                                FL("modify correct segment: from %s got %s")
                                % r % s);
          }
        else
          {
            UNIT_TEST_CHECK_MSG(false,
                                FL("mkstemp failed with template %s "
                                   "(iteration %d, os error %s)")
                                % r % (j+1) % strerror(errno));
            break;
          }
      }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
