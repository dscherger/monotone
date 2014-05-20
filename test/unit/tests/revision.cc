// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../../../src/base.hh"
#include "../unit_tests.hh"
#include "../../../src/revision.hh"
#include "../../../src/lexical_cast.hh"

using std::string;

UNIT_TEST(from_network)
{
  char const * bad_revisions[] = {
    "",

    "format_version \"1\"\n",

    "format_version \"1\"\n"
    "\n"
    "new_manifest [0000000000000000000000000000000000000000]\n",

    "format_version \"1\"\n"
    "\n"
    "new_manifest [000000000000000]\n",

    "format_version \"1\"\n"
    "\n"
    "new_manifest [0000000000000000000000000000000000000000]\n"
    "\n"
    "old_revision [66ff7f4640593afacdb056fefc069349e7d9ed9e]\n"
    "\n"
    "rename \"some_file\"\n"
    "   foo \"x\"\n",

    "format_version \"1\"\n"
    "\n"
    "new_manifest [0000000000000000000000000000000000000000]\n"
    "\n"
    "old_revision [66ff7f4640593afacdb056fefc069349e7d9ed9e]\n"
    "\n"
    "rename \"some_file\"\n"
    "   foo \"some_file\"\n"
  };
  revision_t rev;
  for (unsigned i = 0; i < sizeof(bad_revisions)/sizeof(char const*); ++i)
    {
      UNIT_TEST_CHECKPOINT((string("iteration ")
                            + boost::lexical_cast<string>(i)).c_str());
      UNIT_TEST_CHECK_THROW(read_revision(data(bad_revisions[i],
                                               origin::network),
                                          rev),
                            recoverable_failure);
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
