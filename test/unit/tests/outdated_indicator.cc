// Copyright (C) 2007 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../../../src/base.hh"
#include "../unit_tests.hh"
#include "../../../src/outdated_indicator.hh"

UNIT_TEST(basic)
{
  outdated_indicator indicator;
  {
    outdated_indicator_factory factory;
    UNIT_TEST_CHECK(indicator.outdated());
    indicator = factory.get_indicator();
    UNIT_TEST_CHECK(!indicator.outdated());
    factory.note_change();
    UNIT_TEST_CHECK(indicator.outdated());
    factory.note_change();
    factory.note_change();
    indicator = factory.get_indicator();
    UNIT_TEST_CHECK(!indicator.outdated());
  }
  UNIT_TEST_CHECK(indicator.outdated());
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
