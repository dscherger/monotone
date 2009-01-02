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
#include "dates.hh"
#include "sanity.hh"

#include <ctime>
#include <climits>

// Generic date handling routines for Monotone.
//
// The routines in this file substantively duplicate functionality of the
// standard C library, so one might wonder why they are needed.  There are
// three fundamental portability problems which together force us to
// implement our own date handling:
//
// 1. We want millisecond precision in our dates, and, at the same time, the
//    ability to represent dates far in the future.  Support for dates far
//    in the future (in particular, past 2038) is currently only common on
//    64-bit systems.  Support for sub-second resolution is not available at
//    all in the standard 'broken-down time' format (struct tm).
//
// 2. There is no standardized way to convert from 'struct tm' to 'time_t'
//    without treating the 'struct tm' as local time.  Some systems do
//    provide a 'timegm' function but it is not widespread.
//
// 3. Some (rare, nowadays) systems do not use the Unix epoch as the epoch
//    for 'time_t'.  This is only a problem because we support reading
//    CVS/RCS ,v files, which encode times as decimal seconds since the Unix
//    epoch; so we must support that epoch regardless of what the system does.

using std::string;

// Writing a 64-bit constant is tricky.  We cannot use the macros that
// <stdint.h> provides in C99 (INT64_C, or even INT64_MAX) because those
// macros are not in C++'s version of <stdint.h>.  std::numeric_limits<s64>
// cannot be used directly, so we have to resort to #ifdef chains on the old
// skool C limits macros.  BOOST_STATIC_ASSERT is defined in a way that
// doesn't let us use std::numeric_limits<s64>::max(), so we have to
// postpone checking it until runtime (our_gmtime), bleah. However, the check
// will be optimized out, and the unit tests exercise it.
#if defined LONG_MAX && LONG_MAX > UINT_MAX
  #define PROBABLE_S64_MAX LONG_MAX
  #define s64_C(x) x##L
#elif defined LLONG_MAX && LLONG_MAX > UINT_MAX
  #define PROBABLE_S64_MAX LLONG_MAX
  #define s64_C(x) x##LL
#elif defined LONG_LONG_MAX && LONG_LONG_MAX > UINT_MAX
  #define PROBABLE_S64_MAX LONG_LONG_MAX
  #define s64_C(x) x##LL
#else
  #error "How do I write a constant of type s64?"
#endif

// Our own "struct tm"-like struct to represent broken-down times
struct broken_down_time {
  int millisec;    /* milliseconds (0 - 999) */
  int sec;         /* seconds (0 - 59) */
  int min;         /* minutes (0 - 59) */
  int hour;        /* hours (0 - 23) */
  int day;         /* day of the month (1 - 31) */
  int month;       /* month (1 - 12) */
  int year;        /* years (anno Domini, i.e. 1999) */
};

// The Unix epoch is 1970-01-01T00:00:00 (in UTC).  As we cannot safely
//  assume that the system's epoch is the Unix epoch, we implement the
//  conversion to broken-down time by hand instead of relying on gmtime().
//
// Unix time_t values are a linear count of seconds since the epoch,
// and should be interpreted according to the Gregorian calendar:
//
//  - There are 60 seconds in a minute, 3600 seconds in an hour,
//    86400 seconds in a day.
//  - Years not divisible by 4 have 365 days, or 31536000 seconds.
//  - Years divisible by 4 have 366 days, or 31622400 seconds, except ...
//  - Years divisible by 100 have only 365 days, except ...
//  - Years divisible by 400 have 366 days.
//
//  The last two rules are the Gregorian correction to the Julian calendar.
//  Note that dates before 1582 are treated as if the Gregorian calendar had
//  been in effect on that day in history (the 'proleptic' calendar).  Also,
//  we make no attempt to handle leap seconds.

s64 const INVALID = PROBABLE_S64_MAX;

// This is the date 292278994-01-01T00:00:00.000. The year 292,278,994
// overflows a signed 64-bit millisecond counter somewhere in August, so
// we've rounded down to the last whole year that fits.
s64 const LATEST_SUPPORTED_DATE = s64_C(9223372017129600000);

// This is the date 0001-01-01T00:00:00.000.  There is no year zero in the
// Gregorian calendar, and what are you doing using monotone to version
// data from before the common era, anyway?
s64 const EARLIEST_SUPPORTED_DATE = s64_C(-62135596800000);

// These constants are all in seconds.
u32 const SEC  = 1;
u32 const MIN  = 60*SEC;
u32 const HOUR = 60*MIN;
u64 const DAY  = 24*HOUR;
u64 const YEAR = 365*DAY;

inline s64 MILLISEC(s64 n) { return n * 1000; }

unsigned char const DAYS_PER_MONTH[] = {
  31, // jan
  28, // feb (non-leap)
  31, // mar
  30, // apr
  31, // may
  30, // jun
  31, // jul
  31, // aug
  30, // sep
  31, // oct
  30, // nov
  31, // dec
};

inline bool
is_leap_year(s32 year)
{
  return (year % 4 == 0
    && (year % 100 != 0 || year % 400 == 0));
}
inline s32
days_in_year(s32 year)
{
  return is_leap_year(year) ? 366 : 365;
 }

inline bool
valid_ms_count(s64 d)
{
  return (d >= EARLIEST_SUPPORTED_DATE && d <= LATEST_SUPPORTED_DATE);
}

static void
our_gmtime(s64 ts, broken_down_time & tm)
{
  // validate our assumptions about which basic type is u64 (see above).
  I(PROBABLE_S64_MAX == std::numeric_limits<s64>::max());
  I(LATEST_SUPPORTED_DATE < PROBABLE_S64_MAX);

  I(valid_ms_count(ts));

  // All subsequent calculations are easier if 't' is always positive, so we
  // make zero be EARLIEST_SUPPORTED_DATE, which happens to be
  // 0001-01-01T00:00:00 and is thus a convenient fixed point for leap year
  // calculations.

  u64 t = u64(ts) - u64(EARLIEST_SUPPORTED_DATE);

  // sub-day components
  u64 days = t / MILLISEC(DAY);
  u32 ms_in_day = t % MILLISEC(DAY);

  tm.millisec = ms_in_day % 1000;
  ms_in_day /= 1000;

  tm.sec = ms_in_day % 60;
  ms_in_day /= 60;

  tm.min = ms_in_day % 60;
  tm.hour = ms_in_day / 60;

  // This is the result of inverting the equation
  //    yb = y*365 + y/4 - y/100 + y/400
  // it approximates years since the epoch for any day count.
  u32 year = (400*days / 146097);

  // Compute the _exact_ number of days from the epoch to the beginning of
  // the approximate year determined above.
  u64 yearbeg;
  yearbeg = widen<u64,u32>(year)*365 + year/4 - year/100 + year/400;

  // Our epoch is year 1, not year 0 (there is no year 0).
  year++;

  s64 delta = days - yearbeg;
  // The approximation above occasionally guesses the year before the
  // correct one, but never the year after, or any further off than that.
  if (delta >= days_in_year(year))
    {
      delta -= days_in_year(year);
      year++;
    }
  I(0 <= delta && delta < days_in_year(year));

  tm.year = year;
  days = delta;

  // <yakko> Now, the months digit!
  u32 month = 1;
  for (;;)
    {
      u32 this_month = DAYS_PER_MONTH[month-1];
      if (month == 2 && is_leap_year(year))
        this_month += 1;
      if (days < this_month)
        break;

      days -= this_month;
      month++;
      I(month <= 12);
    }
  tm.month = month;
  tm.day = days + 1;
}

static s64
our_timegm(broken_down_time const & tm)
{
  s64 d;

  // range checks
  I(tm.year  >  0    && tm.year  <= 292278994);
  I(tm.month >= 1    && tm.month <= 12);
  I(tm.day   >= 1    && tm.day   <= 31);
  I(tm.hour  >= 0    && tm.hour  <= 23);
  I(tm.min   >= 0    && tm.min   <= 59);
  I(tm.sec   >= 0    && tm.sec   <= 60);
  I(tm.millisec >= 0 && tm.millisec <= 999);

  // years (since 1970)
  d = YEAR * (tm.year - 1970);
  // leap days to add (or subtract)
  int add_leap_days = (tm.year - 1) / 4 - 492;
  add_leap_days -= (tm.year - 1) / 100 - 19;
  add_leap_days += (tm.year - 1) / 400 - 4;
  d += add_leap_days * DAY;

  // months
  for (int m = 1; m < tm.month; ++m)
    {
      d += DAYS_PER_MONTH[m-1] * DAY;
      if (m == 2 && is_leap_year(tm.year))
        d += DAY;
    }

  // days within month, and so on
  d += (tm.day - 1) * DAY;
  d += tm.hour * HOUR;
  d += tm.min * MIN;
  d += tm.sec * SEC;

  return MILLISEC(d) + tm.millisec;
}

// In a few places we need to know the offset between the Unix epoch and the
// system epoch.
static s64
get_epoch_offset()
{
  static s64 epoch_offset;
  static bool know_epoch_offset = false;
  broken_down_time our_t;

  if (know_epoch_offset)
    return epoch_offset;

  std::time_t epoch = 0;
  std::tm t = *std::gmtime(&epoch);

  our_t.millisec = 0;
  our_t.sec = t.tm_sec;
  our_t.min = t.tm_min;
  our_t.hour = t.tm_hour;
  our_t.day = t.tm_mday;
  our_t.month = t.tm_mon + 1;
  our_t.year = t.tm_year + 1900;

  epoch_offset = our_timegm(our_t);

  L(FL("time epoch offset is %d\n") % epoch_offset);

  know_epoch_offset = true;
  return epoch_offset;
}


//
// date_t methods
//
bool
date_t::valid() const
{
  return valid_ms_count(d);
}

// initialize to an invalid date
date_t::date_t()
  : d(INVALID)
{
  I(!valid());
}

date_t::date_t(s64 d)
  : d(d)
{
  // When initialized from a millisecods since Unix epoch value, we require
  // it to be in a valid range. Use the constructor without any argument to
  // generate an invalid date.
  I(valid());
}

date_t::date_t(int year, int month, int day,
               int hour, int min, int sec, int millisec)
{
  broken_down_time t;
  t.millisec = millisec;
  t.sec = sec;
  t.min = min;
  t.hour = hour;
  t.day = day;
  t.month = month;
  t.year = year;

  d = our_timegm(t);
  I(valid());
}

date_t
date_t::now()
{
  std::time_t t = std::time(0);
  s64 tu = t * 1000 + get_epoch_offset();
  E(valid_ms_count(tu),
    F("current date '%s' is outside usable range\n"
      "(your system clock may not be set correctly)")
    % std::ctime(&t));
  return date_t(tu);
}

string
date_t::as_iso_8601_extended() const
{
  broken_down_time tm;
  I(valid());
  our_gmtime(d, tm);
  return (FL("%04u-%02u-%02uT%02u:%02u:%02u")
             % tm.year % tm.month % tm.day
             % tm.hour % tm.min % tm.sec).str();
}

std::ostream &
operator<< (std::ostream & o, date_t const & d)
{
  return o << d.as_iso_8601_extended();
}

template <> void
dump(date_t const & d, std::string & s)
{
  s = d.as_iso_8601_extended();
}

s64
date_t::as_millisecs_since_unix_epoch() const
{
  return d;
}

// We might want to consider teaching this routine more time formats.
// gnulib has a rather nice date parser, except that it requires Bison
// (not even just yacc).

date_t::date_t(string const & s)
{
  try
    {
      size_t i = s.size() - 1;  // last character of the array

      // check the first character which is not a digit
      while (s.at(i) >= '0' && s.at(i) <= '9')
        i--;

      // ignore fractional seconds, if present, or go back to the end of the
      // date string to parse the digits for seconds.
      if (s.at(i) == '.')
        i--;
      else
        i = s.size() - 1;

      // seconds
      u8 sec;
      N(s.at(i) >= '0' && s.at(i) <= '9'
        && s.at(i-1) >= '0' && s.at(i-1) <= '5',
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      sec = (s.at(i-1) - '0')*10 + (s.at(i) - '0');
      i -= 2;
      N(sec <= 60,
        F("seconds out of range"));

      // optional colon
      if (s.at(i) == ':')
        i--;

      // minutes
      u8 min;
      N(s.at(i) >= '0' && s.at(i) <= '9'
        && s.at(i-1) >= '0' && s.at(i-1) <= '5',
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      min = (s.at(i-1) - '0')*10 + (s.at(i) - '0');
      i -= 2;
      N(min < 60,
        F("minutes out of range"));

      // optional colon
      if (s.at(i) == ':')
        i--;

      // hours
      u8 hour;
      N((s.at(i-1) >= '0' && s.at(i-1) <= '1'
         && s.at(i) >= '0' && s.at(i) <= '9')
        || (s.at(i-1) == '2' && s.at(i) >= '0' && s.at(i) <= '3'),
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      hour = (s.at(i-1) - '0')*10 + (s.at(i) - '0');
      i -= 2;
      N(hour < 24,
        F("hour out of range"));

      // We accept 'T' as well as spaces between the date and the time
      N(s.at(i) == 'T' || s.at(i) == ' ',
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      i--;

      // day
      u8 day;
      N(s.at(i-1) >= '0' && s.at(i-1) <= '3'
        && s.at(i) >= '0' && s.at(i) <= '9',
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      day = (s.at(i-1) - '0')*10 + (s.at(i) - '0');
      i -= 2;

      // optional dash
      if (s.at(i) == '-')
        i--;

      // month
      u8 month;
      N(s.at(i-1) >= '0' && s.at(i-1) <= '1'
        && s.at(i) >= '0' && s.at(i) <= '9',
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      month = (s.at(i-1) - '0')*10 + (s.at(i) - '0');
      N(month >= 1 && month <= 12,
        F("month out of range in '%s'") % s);
      i -= 2;

      // optional dash
      if (s.at(i) == '-')
        i--;

      // year
      N(i >= 3,
        F("unrecognized date (monotone only understands ISO 8601 format)"));

      // this counts down through zero and stops when it wraps around
      // (size_t being unsigned)
      u32 year = 0;
      u32 digit = 1;
      while (i < s.size())
        {
          N(s.at(i) >= '0' && s.at(i) <= '9',
            F("unrecognized date (monotone only understands ISO 8601 format)"));
          year += (s.at(i) - '0')*digit;
          i--;
          digit *= 10;
        }

      N(year >= 1,
        F("date too early (monotone only goes back to 0001-01-01T00:00:00)"));
      N(year <= 292278994,
        F("date too late (monotone only goes forward to year 292,278,993)"));

      u8 mdays;
      if (month == 2 && is_leap_year(year))
        mdays = DAYS_PER_MONTH[month-1] + 1;
      else
        mdays = DAYS_PER_MONTH[month-1];

      N(day >= 1 && day <= mdays,
        F("day out of range for its month in '%s'") % s);

      broken_down_time t;
      t.millisec = 0;
      t.sec = sec;
      t.min = min;
      t.hour = hour;
      t.day = day;
      t.month = month;
      t.year = year;

      d = our_timegm(t);
    }
  catch (std::out_of_range)
    {
      N(false,
        F("unrecognized date (monotone only understands ISO 8601 format)"));
    }
}

date_t &
date_t::operator +=(s64 const other)
{
  // only operate on vaild dates, prevent turning an invalid
  // date into a valid one.
  I(valid());

  d += other;

  I(valid());

  return *this;
}

date_t &
date_t::operator -=(s64 const other)
{
  // simply use the addition operator with inversed sign
  return operator+=(-other);
}

date_t
date_t::operator +(s64 const other) const
{
  date_t result(d);
  result += other;
  return result;
}

date_t
date_t::operator -(s64 const other) const
{
  date_t result(d);
  result += -other;
  return result;
}

s64
date_t::operator -(date_t const & other) const
{
  return d - other.d;
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

s64 const FOUR_HUNDRED_YEARS = 400 * YEAR + (100 - 4 + 1) * DAY;

UNIT_TEST(date, our_timegm)
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

UNIT_TEST(date, from_string)
{
#define OK(x,y) do {                                    \
    string s_ = date_t(x).as_iso_8601_extended();       \
    L(FL("date_t: %s -> %s") % (x) % s_);               \
    UNIT_TEST_CHECK(s_ == (y));                         \
  } while (0)
#define NO(x) UNIT_TEST_CHECK_THROW(date_t(x), informative_failure)

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

UNIT_TEST(date, from_unix_epoch)
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

UNIT_TEST(date, comparisons)
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

UNIT_TEST(date, roundtrip_all_year_boundaries)
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
#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
