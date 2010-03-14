// Copyright (C) 2007, 2008 Zack Weinberg <zackw@panix.com>
//                          Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "unit_tests.hh"

#include "../dates.cc"

s64 const FOUR_HUNDRED_YEARS = 400 * YEAR + (100 - 4 + 1) * DAY;

UNIT_TEST(our_timegm)
{
#define OK(x) UNIT_TEST_CHECK(our_timegm(t) == MILLISEC(x))

  broken_down_time t = {0, 0, 0, 0, 1, 1, 1970};
  OK(0);

  t.year = 2000;
  OK(s64_C(946684800));

  // Make sure our_timegm works for years before 1970 as well.
  t.year = 1960;
  OK(-10 * YEAR - 3 * DAY);

  t.year = 1569;
  OK(-FOUR_HUNDRED_YEARS - YEAR);

  t.year = 1570;
  OK(-FOUR_HUNDRED_YEARS);

  t.year = 1571;
  OK(-FOUR_HUNDRED_YEARS + YEAR);

  t.year = 1572;
  OK(-FOUR_HUNDRED_YEARS + 2 * YEAR);

  t.year = 1573;
  OK(-FOUR_HUNDRED_YEARS + 3 * YEAR + DAY);

  t.year = 1574;
  OK(-FOUR_HUNDRED_YEARS + 4 * YEAR + DAY);

  t.year = 1170;
  OK(-2 * FOUR_HUNDRED_YEARS);

  t.year = 770;
  OK(-3 * FOUR_HUNDRED_YEARS);

  t.year = 370;
  OK(-4 * FOUR_HUNDRED_YEARS);

  t.year = 1;  /* year 1 anno Domini */
  OK(-1969 * YEAR - (492 - 19 + 4) * DAY);

  t.year = 0; /* no such year */
  UNIT_TEST_CHECK_THROW(our_timegm(t), std::logic_error);

#undef OK
}

UNIT_TEST(from_string)
{
#define OK(x,y) do {                                    \
    string s_ = date_t(x).as_iso_8601_extended();       \
    L(FL("date_t: %s -> %s") % (x) % s_);               \
    UNIT_TEST_CHECK(s_ == (y));                         \
  } while (0)
#define NO(x) UNIT_TEST_CHECK_THROW(date_t(x), recoverable_failure)

  // canonical format
  OK("2007-03-01T18:41:13", "2007-03-01T18:41:13");
  OK("2007-03-01T00:41:13", "2007-03-01T00:41:13");
  OK("2007-03-01T01:41:13", "2007-03-01T01:41:13");
  OK("2007-03-01T23:41:13", "2007-03-01T23:41:13");

  // test dates around leap years
  OK("1999-12-31T12:00:00", "1999-12-31T12:00:00");
  OK("1999-12-31T23:59:59", "1999-12-31T23:59:59");
  OK("2000-01-01T00:00:00", "2000-01-01T00:00:00");
  OK("2000-01-01T12:00:00", "2000-01-01T12:00:00");
  OK("2003-12-31T12:00:00", "2003-12-31T12:00:00");
  OK("2003-12-31T23:59:59", "2003-12-31T23:59:59");
  OK("2004-01-01T00:00:00", "2004-01-01T00:00:00");
  OK("2004-01-01T12:00:00", "2004-01-01T12:00:00");

  // test dates around the leap day in february
  OK("2003-02-28T23:59:59", "2003-02-28T23:59:59");
  NO("2003-02-29T00:00:00");
  OK("2004-02-28T23:59:59", "2004-02-28T23:59:59");
  OK("2004-02-29T00:00:00", "2004-02-29T00:00:00");

  // squashed format
  OK("20070301T184113", "2007-03-01T18:41:13");
  // space between date and time
  OK("2007-03-01 18:41:13", "2007-03-01T18:41:13");
  // squashed, space
  OK("20070301 184113", "2007-03-01T18:41:13");

  // more than four digits in the year
  OK("120070301T184113", "12007-03-01T18:41:13");

  // before the epoch
  OK("1969-03-01T18:41:13", "1969-03-01T18:41:13");

  // inappropriate character at every possible position
  NO("x007-03-01T18:41:13");
  NO("2x07-03-01T18:41:13");
  NO("20x7-03-01T18:41:13");
  NO("200x-03-01T18:41:13");
  NO("2007x03-01T18:41:13");
  NO("2007-x3-01T18:41:13");
  NO("2007-0x-01T18:41:13");
  NO("2007-03x01T18:41:13");
  NO("2007-03-x1T18:41:13");
  NO("2007-03-0xT18:41:13");
  NO("2007-03-01x18:41:13");
  NO("2007-03-01Tx8:41:13");
  NO("2007-03-01T1x:41:13");
  NO("2007-03-01T18x41:13");
  NO("2007-03-01T18:x1:13");
  NO("2007-03-01T18:4x:13");
  NO("2007-03-01T18:41x13");
  NO("2007-03-01T18:41:x3");
  NO("2007-03-01T18:41:1x");

  NO("x0070301T184113");
  NO("2x070301T184113");
  NO("20x70301T184113");
  NO("200x0301T184113");
  NO("2007x301T184113");
  NO("20070x01T184113");
  NO("200703x1T184113");
  NO("2007030xT184113");
  NO("20070301x184113");
  NO("20070301Tx84113");
  NO("20070301T1x4113");
  NO("20070301T18x113");
  NO("20070301T184x13");
  NO("20070301T1841x3");
  NO("20070301T18411x");

  // two digit years are not accepted
  NO("07-03-01T18:41:13");

  // components (other than year) out of range
  NO("2007-00-01T18:41:13");
  NO("2007-13-01T18:41:13");

  NO("2007-01-00T18:41:13");
  NO("2007-01-32T18:41:13");
  NO("2007-02-29T18:41:13");
  NO("2007-03-32T18:41:13");
  NO("2007-04-31T18:41:13");
  NO("2007-05-32T18:41:13");
  NO("2007-06-31T18:41:13");
  NO("2007-07-32T18:41:13");
  NO("2007-08-32T18:41:13");
  NO("2007-09-31T18:41:13");
  NO("2007-10-32T18:41:13");
  NO("2007-11-31T18:41:13");
  NO("2007-03-32T18:41:13");

  NO("2007-03-01T24:41:13");
  NO("2007-03-01T18:60:13");
  NO("2007-03-01T18:41:60");

  // leap year February
  OK("2008-02-29T18:41:13", "2008-02-29T18:41:13");
  NO("2008-02-30T18:41:13");

  // maybe we should support these, but we don't
  NO("2007-03-01");
  NO("18:41");
  NO("18:41:13");
  NO("Thu Mar 1 18:41:13 PST 2007");
  NO("Thu, 01 Mar 2007 18:47:22");
  NO("Thu, 01 Mar 2007 18:47:22 -0800");
  NO("torsdag, mars 01, 2007, 18.50.10");
  // et cetera
#undef OK
#undef NO
}

UNIT_TEST(roundtrip_localtimes)
{
#define OK(x) do {                                                       \
    string iso8601 = x.as_iso_8601_extended();                           \
    string formatted = x.as_formatted_localtime("%c");                   \
    L(FL("iso 8601 date '%s' local date '%s'\n") % iso8601 % formatted); \
    date_t parsed = date_t::from_formatted_localtime(formatted, "%c");   \
    UNIT_TEST_CHECK(parsed == x);                                        \
  } while (0)

  // this is the valid range of dates supported by 32 bit time_t
  date_t start("1901-12-13T20:45:52");
  date_t end("2038-01-19T03:14:07");

  OK(start);
  OK(end);

  // stagger the millisecond values to hit different times of day
  for (date_t date = start; date <= end; date += MILLISEC(DAY+HOUR+MIN+SEC))
    OK(date);

  start -= 1000;
  end += 1000;

  // these tests run with LANG=C and TZ=UTC so the %c format seems to work
  // however strptime does not like the timezone name when %c is used in 
  // other locales. with LANG=en_CA.UTF-8 this test fails.

  if (sizeof(time_t) <= 4)
    {
      UNIT_TEST_CHECK_THROW(start.as_formatted_localtime("%c"),
                            recoverable_failure);
      UNIT_TEST_CHECK_THROW(date_t::from_formatted_localtime("Fri Dec 13 20:45:51 1901", "%c"),
                            recoverable_failure);

      UNIT_TEST_CHECK_THROW(end.as_formatted_localtime("%c"),
                            recoverable_failure);
      UNIT_TEST_CHECK_THROW(date_t::from_formatted_localtime("Tue Jan 19 03:14:08 2038", "%c"),
                            recoverable_failure);
    }
  else
    {
      OK(start);
      OK(end);
    }

  // this date represents 1 second before the unix epoch which has a time_t
  // value of -1. mktime returns -1 to indicate that it was unable to
  // convert a struct tm into a valid time_t value even though dates
  // before/after this date are valid.
  date_t mktime1("1969-12-31T23:59:59");

  // this can be formatted but not parsed. 64 bit time_t probably doesn't help
  mktime1.as_formatted_localtime("%c");
  UNIT_TEST_CHECK_THROW(date_t::from_formatted_localtime("Wed Dec 31 23:59:59 1969", "%c"),
                        recoverable_failure);
#undef OK
}
UNIT_TEST(from_unix_epoch)
{
#define OK_(x,y) do {                              \
    string s_ = date_t(x).as_iso_8601_extended();  \
    L(FL("date_t: %lu -> %s") % (x) % s_);         \
    UNIT_TEST_CHECK(s_ == (y));                    \
  } while (0)
#define OK(x,y) OK_(s64_C(x),y)

  // every month boundary in 1970
  OK(0,          "1970-01-01T00:00:00");
  OK(2678399000, "1970-01-31T23:59:59");
  OK(2678400000, "1970-02-01T00:00:00");
  OK(5097599000, "1970-02-28T23:59:59");
  OK(5097600000, "1970-03-01T00:00:00");
  OK(7775999000, "1970-03-31T23:59:59");
  OK(7776000000, "1970-04-01T00:00:00");
  OK(10367999000, "1970-04-30T23:59:59");
  OK(10368000000, "1970-05-01T00:00:00");
  OK(13046399000, "1970-05-31T23:59:59");
  OK(13046400000, "1970-06-01T00:00:00");
  OK(15638399000, "1970-06-30T23:59:59");
  OK(15638400000, "1970-07-01T00:00:00");
  OK(18316799000, "1970-07-31T23:59:59");
  OK(18316800000, "1970-08-01T00:00:00");
  OK(20995199000, "1970-08-31T23:59:59");
  OK(20995200000, "1970-09-01T00:00:00");
  OK(23587199000, "1970-09-30T23:59:59");
  OK(23587200000, "1970-10-01T00:00:00");
  OK(26265599000, "1970-10-31T23:59:59");
  OK(26265600000, "1970-11-01T00:00:00");
  OK(28857599000, "1970-11-30T23:59:59");
  OK(28857600000, "1970-12-01T00:00:00");
  OK(31535999000, "1970-12-31T23:59:59");
  OK(31536000000, "1971-01-01T00:00:00");

  // every month boundary in 1972 (an ordinary leap year)
  OK(63071999000, "1971-12-31T23:59:59");
  OK(63072000000, "1972-01-01T00:00:00");
  OK(65750399000, "1972-01-31T23:59:59");
  OK(65750400000, "1972-02-01T00:00:00");
  OK(68255999000, "1972-02-29T23:59:59");
  OK(68256000000, "1972-03-01T00:00:00");
  OK(70934399000, "1972-03-31T23:59:59");
  OK(70934400000, "1972-04-01T00:00:00");
  OK(73526399000, "1972-04-30T23:59:59");
  OK(73526400000, "1972-05-01T00:00:00");
  OK(76204799000, "1972-05-31T23:59:59");
  OK(76204800000, "1972-06-01T00:00:00");
  OK(78796799000, "1972-06-30T23:59:59");
  OK(78796800000, "1972-07-01T00:00:00");
  OK(81475199000, "1972-07-31T23:59:59");
  OK(81475200000, "1972-08-01T00:00:00");
  OK(84153599000, "1972-08-31T23:59:59");
  OK(84153600000, "1972-09-01T00:00:00");
  OK(86745599000, "1972-09-30T23:59:59");
  OK(86745600000, "1972-10-01T00:00:00");
  OK(89423999000, "1972-10-31T23:59:59");
  OK(89424000000, "1972-11-01T00:00:00");
  OK(92015999000, "1972-11-30T23:59:59");
  OK(92016000000, "1972-12-01T00:00:00");
  OK(94694399000, "1972-12-31T23:59:59");
  OK(94694400000, "1973-01-01T00:00:00");

  // every month boundary in 2000 (a leap year per rule 5)
  OK(946684799000, "1999-12-31T23:59:59");
  OK(946684800000, "2000-01-01T00:00:00");
  OK(949363199000, "2000-01-31T23:59:59");
  OK(949363200000, "2000-02-01T00:00:00");
  OK(951868799000, "2000-02-29T23:59:59");
  OK(951868800000, "2000-03-01T00:00:00");
  OK(954547199000, "2000-03-31T23:59:59");
  OK(954547200000, "2000-04-01T00:00:00");
  OK(957139199000, "2000-04-30T23:59:59");
  OK(957139200000, "2000-05-01T00:00:00");
  OK(959817599000, "2000-05-31T23:59:59");
  OK(959817600000, "2000-06-01T00:00:00");
  OK(962409599000, "2000-06-30T23:59:59");
  OK(962409600000, "2000-07-01T00:00:00");
  OK(965087999000, "2000-07-31T23:59:59");
  OK(965088000000, "2000-08-01T00:00:00");
  OK(967766399000, "2000-08-31T23:59:59");
  OK(967766400000, "2000-09-01T00:00:00");
  OK(970358399000, "2000-09-30T23:59:59");
  OK(970358400000, "2000-10-01T00:00:00");
  OK(973036799000, "2000-10-31T23:59:59");
  OK(973036800000, "2000-11-01T00:00:00");
  OK(975628799000, "2000-11-30T23:59:59");
  OK(975628800000, "2000-12-01T00:00:00");
  OK(978307199000, "2000-12-31T23:59:59");
  OK(978307200000, "2001-01-01T00:00:00");

  // every month boundary in 2100 (a normal year per rule 4)
  OK(4102444800000, "2100-01-01T00:00:00");
  OK(4105123199000, "2100-01-31T23:59:59");
  OK(4105123200000, "2100-02-01T00:00:00");
  OK(4107542399000, "2100-02-28T23:59:59");
  OK(4107542400000, "2100-03-01T00:00:00");
  OK(4110220799000, "2100-03-31T23:59:59");
  OK(4110220800000, "2100-04-01T00:00:00");
  OK(4112812799000, "2100-04-30T23:59:59");
  OK(4112812800000, "2100-05-01T00:00:00");
  OK(4115491199000, "2100-05-31T23:59:59");
  OK(4115491200000, "2100-06-01T00:00:00");
  OK(4118083199000, "2100-06-30T23:59:59");
  OK(4118083200000, "2100-07-01T00:00:00");
  OK(4120761599000, "2100-07-31T23:59:59");
  OK(4120761600000, "2100-08-01T00:00:00");
  OK(4123439999000, "2100-08-31T23:59:59");
  OK(4123440000000, "2100-09-01T00:00:00");
  OK(4126031999000, "2100-09-30T23:59:59");
  OK(4126032000000, "2100-10-01T00:00:00");
  OK(4128710399000, "2100-10-31T23:59:59");
  OK(4128710400000, "2100-11-01T00:00:00");
  OK(4131302399000, "2100-11-30T23:59:59");
  OK(4131302400000, "2100-12-01T00:00:00");
  OK(4133980799000, "2100-12-31T23:59:59");

  // limit of valid dates
  OK_(LATEST_SUPPORTED_DATE, "292278994-01-01T00:00:00");
  UNIT_TEST_CHECK_THROW(date_t(LATEST_SUPPORTED_DATE+1),
                        std::logic_error);
  OK_(EARLIEST_SUPPORTED_DATE, "0001-01-01T00:00:00");
  UNIT_TEST_CHECK_THROW(date_t(EARLIEST_SUPPORTED_DATE-1),
                        std::logic_error);

#undef OK
}

UNIT_TEST(comparisons)
{
  date_t may = date_t("2000-05-01T00:00:00"),
         jun = date_t("2000-06-01T00:00:00"),
         jul = date_t("2000-07-01T00:00:00"),
         v;

  // testing all comparisons operators
  UNIT_TEST_CHECK(may < jun);
  UNIT_TEST_CHECK(jun < jul);
  UNIT_TEST_CHECK(may < jul);

  UNIT_TEST_CHECK(jul > may);

  UNIT_TEST_CHECK(may == date_t("2000-05-01T00:00:00"));
  UNIT_TEST_CHECK(may != date_t("2000-05-01T00:00:01"));
  UNIT_TEST_CHECK(may != date_t("2000-09-01T00:00:00"));
  UNIT_TEST_CHECK(may != date_t("1999-05-01T00:00:00"));

  v = may;
  v += MILLISEC(DAY * 31);
  UNIT_TEST_CHECK(v == jun);

  v = jul;
  v -= MILLISEC(DAY * 30);
  UNIT_TEST_CHECK(v == jun);

  // check limits for subtractions
  v = date_t("0001-01-01T00:00:01");
  v -= 1000;
  UNIT_TEST_CHECK(v == date_t("0001-01-01T00:00:00"));
  UNIT_TEST_CHECK_THROW(v -= 1, std::logic_error);

  // check limits for additions
  v = date_t("292278993-12-31T23:59:59");
  v += 1000;
  UNIT_TEST_CHECK(v == date_t("292278994-01-01T00:00:00"));
  L(FL("v off by %ld")
    % (v.as_millisecs_since_unix_epoch() - LATEST_SUPPORTED_DATE));
  UNIT_TEST_CHECK_THROW(v += 1, std::logic_error);

  // check date differences
  UNIT_TEST_CHECK(date_t("2000-05-05T00:00:01") -
                  date_t("2000-05-05T00:00:00")
                  == 1000);
  UNIT_TEST_CHECK(date_t("2000-05-05T00:00:01") -
                  date_t("2000-05-05T00:00:02")
                  == -1000);
  UNIT_TEST_CHECK(date_t("2000-05-05T01:00:00") -
                  date_t("2000-05-05T00:00:00")
                  == 3600000);
}

// This test takes a long time to run and can create an enormous logfile
// (if there are a lot of failures) so it is disabled by default.  If you
// make substantial changes to our_gmtime or our_timegm you should run it.
#if 0
static void
roundtrip_1(s64 t)
{
  if (!valid_ms_count(t))
    return;

  broken_down_time tm;
  our_gmtime(t, tm);
  s64 t1 = our_timegm(tm);
  if (t != t1)
    {
      L(FL("%d -> %04u-%02u-%02uT%02u:%02u:%02u.%03u -> %d error %+d")
        % t
        % tm.year % tm.month % tm.day % tm.hour % tm.min % tm.sec % tm.millisec
        % t1
        % (t - t1));
      UNIT_TEST_CHECK(t == t1);
    }

  // if possible check against the system gmtime() as well
  if (std::numeric_limits<time_t>::max() >= std::numeric_limits<s64>::max())
    {
      time_t tsys = ((t - tm.millisec) / 1000) - get_epoch_offset();
      std::tm tmsys = *std::gmtime(&tsys);
      broken_down_time tmo;
      tmo.millisec = 0;
      tmo.sec = tmsys.tm_sec;
      tmo.min = tmsys.tm_min;
      tmo.hour = tmsys.tm_hour;
      tmo.day = tmsys.tm_mday;
      tmo.month = tmsys.tm_mon + 1;
      tmo.year = tmsys.tm_year + 1900;

      bool sys_match = (tm.year == tmo.year
                        && tm.month == tmo.month
                        && tm.day == tmo.day
                        && tm.hour == tmo.hour
                        && tm.min == tmo.min
                        && tm.sec == tmo.sec);
      if (!sys_match)
        {
          L(FL("ours: %04u-%02u-%02uT%02u:%02u:%02u.%03u")
            % tm.year % tm.month % tm.day % tm.hour % tm.min
            % tm.sec % tm.millisec);
          L(FL("system: %04u-%02u-%02uT%02u:%02u:%02u")
            % tmo.year % tmo.month % tmo.day % tmo.hour % tmo.min % tmo.sec);
          UNIT_TEST_CHECK(sys_match);
        }
    }
}

UNIT_TEST(roundtrip_all_year_boundaries)
{
  s64 t = EARLIEST_SUPPORTED_DATE;
  u32 year = 1;

  while (t < LATEST_SUPPORTED_DATE)
    {
      roundtrip_1(t-1);
      roundtrip_1(t);

      t += MILLISEC(DAY * days_in_year(year));
      year ++;
    }
}
#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
