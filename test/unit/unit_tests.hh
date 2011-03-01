// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __UNIT_TESTS__
#define __UNIT_TESTS__

#include <boost/noncopyable.hpp>

// Log a success/failure message, and set the test state to 'fail' if needed
#define UNIT_TEST_CHECK(expression)             \
  unit_test::do_check(expression, __FILE__, __LINE__, #expression)

// Like UNIT_TEST_CHECK, but you get to specify what is logged.
// MSG should be an FL("...") % ... construct.
#define UNIT_TEST_CHECK_MSG(expression, msg)              \
  unit_test::do_check(expression, __FILE__, __LINE__, (msg).str().c_str())

// Like UNIT_TEST_CHECK, but abort the test immediately on failure
#define UNIT_TEST_REQUIRE(expression)           \
  unit_test::do_require(expression, __FILE__, __LINE__, #expression)

#define UNIT_TEST_CHECK_THROW(statement, exception)                     \
  do                                                                    \
    {                                                                   \
      bool fnord_unit_test_checkval = false;                            \
      try                                                               \
        {                                                               \
          statement;                                                    \
        }                                                               \
      catch(exception const &)                                          \
        {                                                               \
          fnord_unit_test_checkval = true;                              \
        }                                                               \
      unit_test::do_check(fnord_unit_test_checkval, __FILE__, __LINE__, \
                          #statement " throws " #exception);            \
    } while (0)

#define UNIT_TEST_CHECK_NOT_THROW(statement, exception)                 \
  do                                                                    \
    {                                                                   \
      bool fnord_unit_test_checkval = true;                             \
      try                                                               \
        {                                                               \
          statement;                                                    \
        }                                                               \
      catch(exception const &)                                          \
        {                                                               \
          fnord_unit_test_checkval = false;                             \
        }                                                               \
      unit_test::do_check(fnord_unit_test_checkval, __FILE__, __LINE__, \
                          #statement " does not throw " #exception);    \
    } while (0)

#define UNIT_TEST_CHECKPOINT(message)           \
  unit_test::do_checkpoint(__FILE__, __LINE__, message);


namespace unit_test {
  void do_check(bool checkval, char const * file,
                int line, char const * message);

  void do_require(bool checkval, char const * file,
                  int line, char const * message);

  void do_checkpoint(char const * file, int line, char const * message);

  // Declarative mechanism for specifying unit tests, similar to
  // auto_unit_test in boost, but more suited to our needs.
  struct unit_test_case : boost::noncopyable
  {
    std::string const group;
    std::string const name;
    void (*const func)();
    bool const failure_is_success;
    unit_test_case(char const * group,
                   char const * name,
                   void (*func)(),
                   bool fis);
  };
}

// The names of the test functions must not collide with each other or with
// names of symbols in the code being tested, despite their being in a
// separate namespace, so that references _from_ the test functions _to_ the
// code under test resolve correctly.
#define UNIT_TEST(TEST)                         \
  namespace unit_test {                         \
      static void t_##TEST();                   \
      static const unit_test_case r_##TEST      \
      (__FILE__, #TEST, t_##TEST, false);       \
  }                                             \
  static void unit_test::t_##TEST()

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
