// Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../../../src/base.hh"
#include "../unit_tests.hh"
#include "../../../src/option.hh"

using std::string;
using std::vector;

UNIT_TEST(concrete_options)
{
  bool b = false;
  string s;
  int i = -1;
  vector<string> v;

  option::concrete_option_set os;
  os("--", "", option::setter(v), option::resetter(v))
    ("bool,b", "", option::setter(b), option::resetter(b, false))
    ("s", "", option::setter(s))
    ("int", "", option::setter(i));

  {
    char const * cmdline[] = {"progname", "pos", "-s", "str ing", "--int", "10",
                              "--int", "45", "--", "--bad", "foo", "-b"};
    os.from_command_line(12, cmdline);
  }
  UNIT_TEST_CHECK(!b);
  UNIT_TEST_CHECK(i == 45);
  UNIT_TEST_CHECK(s == "str ing");
  UNIT_TEST_CHECK(v.size() == 4);// pos --bad foo -b
  os.reset();
  UNIT_TEST_CHECK(v.empty());

  {
    args_vector cmdline;
    cmdline.push_back(arg_type("--bool", origin::internal));
    cmdline.push_back(arg_type("-s", origin::internal));
    cmdline.push_back(arg_type("-s", origin::internal));
    cmdline.push_back(arg_type("foo", origin::internal));
    os.from_command_line(cmdline);
  }
  UNIT_TEST_CHECK(b);
  UNIT_TEST_CHECK(s == "-s");
  UNIT_TEST_CHECK(v.size() == 1);
  UNIT_TEST_CHECK(v[0] == "foo");
  os.reset();
  UNIT_TEST_CHECK(!b);

  {
    char const * cmdline[] = {"progname", "--bad_arg", "x"};
    UNIT_TEST_CHECK_THROW(os.from_command_line(3, cmdline), option::unknown_option);
  }

  {
    char const * cmdline[] = {"progname", "--bool=x"};
    UNIT_TEST_CHECK_THROW(os.from_command_line(2, cmdline), option::extra_arg);
  }

  {
    char const * cmdline[] = {"progname", "-bx"};
    UNIT_TEST_CHECK_THROW(os.from_command_line(2, cmdline), option::extra_arg);
  }

  {
    char const * cmdline[] = {"progname", "-s"};
    UNIT_TEST_CHECK_THROW(os.from_command_line(2, cmdline), option::missing_arg);
  }

  {
    char const * cmdline[] = {"progname", "--int=x"};
    UNIT_TEST_CHECK_THROW(os.from_command_line(2, cmdline), option::bad_arg);
  }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
