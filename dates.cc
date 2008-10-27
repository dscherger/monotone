// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "dates.hh"

#include <ctime>
#include <climits>

using std::string;

// Writing a 64-bit constant is tricky.  We cannot use the macros that
// <stdint.h> provides in C99 (UINT64_C, or even UINT64_MAX) because those
// macros are not in C++'s version of <stdint.h>.  std::numeric_limits<u64>
// cannot be used directly, so we have to resort to #ifdef chains on the old
// skool C limits macros.  BOOST_STATIC_ASSERT is defined in a way that
// doesn't let us use std::numeric_limits<u64>::max(), so we have to
// postpone checking it until runtime (our_gmtime), bleah. However, the check
// will be optimized out, and the unit tests exercise it.
#if defined ULONG_MAX && ULONG_MAX > UINT_MAX
  #define PROBABLE_U64_MAX ULONG_MAX
  #define u64_C(x) x##UL
#elif defined ULLONG_MAX && ULLONG_MAX > UINT_MAX
  #define PROBABLE_U64_MAX ULLONG_MAX
  #define u64_C(x) x##ULL
#elif defined ULONG_LONG_MAX && ULONG_LONG_MAX > UINT_MAX
  #define PROBABLE_U64_MAX ULONG_LONG_MAX
  #define u64_C(x) x##ULL
#else
  #error "How do I write a constant of type u64?"
#endif

// Forward declarations required so as to not have to shuffle around code.
static s64 our_mktime(broken_down_time const & tm);
static void our_gmtime(const s64 d, broken_down_time & tm);

date_t::date_t(u64 d)
  : d(d)
{
  // When initialized from a millisecods since Unix epoch value, we require
  // that to be in a valid range. Use the constructor without any argument
  // to generate an invalid date.
  I(valid());
}

date_t::date_t(int year, int month, int day,
               int hour, int min, int sec, int millisec)
{
  // general validity checks
  I((year >= 1970) && (year <= 9999));
  I((month >= 1) && (month <= 12));
  I((day >= 1) && (day <= 31));
  I((hour >= 0) && (hour < 24));
  I((min >= 0) && (min < 60));
  I((sec >= 0) && (sec < 60));
  I((millisec >= 0) && (millisec < 1000));

  broken_down_time t;
  t.millisec = millisec;
  t.sec = sec;
  t.min = min;
  t.hour = hour;
  t.day = day;
  t.month = month - 1;
  t.year = year - 1900;

  d = our_mktime(t);
  I(valid());
}

bool
date_t::valid() const
{
  // year 10000 limit
  return d < u64_C(253402300800000);
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

// The Unix epoch is 1970-01-01T00:00:00 (in UTC).  As we cannot safely
//  assume that the system's epoch is the Unix epoch, we implement the
//  conversion to broken-down time by hand instead of relying on gmtime().
//  The algorithm below has been tested on one value from every day in the
//  range [1970-01-01T00:00:00, 36812-02-20T00:36:16) -- that is, [0, 2**40).
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
//  We make no attempt to handle leap seconds.

u64 const SEC = u64_C(1000);
u64 const MIN = 60 * SEC;
u64 const HOUR = 60 * MIN;
u64 const DAY = 24 * HOUR;
u64 const YEAR = 365 * DAY;
u64 const LEAP = 366 * DAY;
u64 const FOUR_HUNDRED_YEARS = 400 * YEAR + (100 - 4 + 1) * DAY;

unsigned char const MONTHS[] = {
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
  our_t.month = t.tm_mon;
  our_t.year = t.tm_year;

  epoch_offset = our_mktime(our_t);

  know_epoch_offset = true;
  return epoch_offset;
}

date_t
date_t::now()
{
  std::time_t t = std::time(0);
  date_t d;
  d.d = u64(t) * 1000 + get_epoch_offset();
  return d;
}

inline bool
is_leap_year(unsigned int year)
{
  return (year % 4 == 0
    && (year % 100 != 0 || year % 400 == 0));
}
inline u64
millisecs_in_year(unsigned int year)
{
  return is_leap_year(year) ? LEAP : YEAR;
}

string
date_t::as_iso_8601_extended() const
{
  broken_down_time tm;
  I(valid());
  our_gmtime(d, tm);
  return (FL("%04u-%02u-%02uT%02u:%02u:%02u")
             % (tm.year + 1900) % (tm.month + 1) % tm.day
             % tm.hour % tm.min % tm.sec).str();
}

u64
date_t::millisecs_since_unix_epoch() const
{
  return d;
}

static void
our_gmtime(const s64 d, broken_down_time & tm)
{
  // these types hint to the compiler that narrowing divides are safe
  u64 yearbeg;
  u16 year;
  u32 month;
  u32 day;
  u32 msofday;
  u16 hour;
  u32 msofhour;
  u8 min;
  u16 msofmin;
  u8 sec;
  u16 msec;

  // the temp variable to calculate with
  u64 t = d;

  // validate our assumptions about which basic type is u64 (see above).
  I(PROBABLE_U64_MAX == std::numeric_limits<u64>::max());

  // enforce a limit of year 9999 so that we remain within the range of a
  // four digit year.
  I(t < u64_C(253402300800000));

  // There are 31556952 seconds (365d 5h 43m 12s) in the average Gregorian
  // year.  This will therefore approximate the correct year (minus 1970).
  // It may be off in either direction, but by no more than one year
  // (empirically tested for every year from 1970 to 2**32 - 1).
  year = t / u64_C(31556592000);

  // Given the above approximation, recalculate the _exact_ number of
  // milliseconds to the beginning of that year.  For this to work correctly
  // (i.e. for the year/4, year/100, year/400 terms to increment exactly
  // when they ought to) it is necessary to count years from 1601 (as if the
  // Gregorian calendar had been in effect at that time) and then correct
  // the final number of milliseconds back to the 1970 epoch.
  year += 369;

  yearbeg = (widen<u64,u32>(year)*365 + year/4 - year/100 + year/400) * DAY;
  yearbeg -= (widen<u64,u32>(369)*365 +  369/4 -  369/100 +  369/400) * DAY;

  // *now* we want year to have its true value.
  year += 1601;

  // Linear search for the range of milliseconds that really contains t.
  // Due to the above approximation it's sufficient to correct only once
  // in one or the other direction.
  if (yearbeg > t)
    yearbeg -= millisecs_in_year(--year);
  else if (yearbeg + millisecs_in_year(year) <= t)
    yearbeg += millisecs_in_year(year++);
  I((yearbeg <= t) && (yearbeg + millisecs_in_year(year) > t));

  t -= yearbeg;

  // <yakko> Now, the months digit!
  month = 0;
  for (;;)
    {
      u64 this_month = MONTHS[month] * DAY;
      if (month == 1 && is_leap_year(year))
        this_month += DAY;
      if (t < this_month)
        break;

      t -= this_month;
      month++;
      I(month < 12);
    }

  // the rest is straightforward.
  day = t / DAY;
  msofday = t % DAY;

  hour = msofday / HOUR;
  msofhour = msofday % HOUR;

  min = msofhour / MIN;
  msofmin = msofhour % MIN;

  sec = msofmin / SEC;
  msec = msofmin % SEC;

  // fill in the result
  tm.millisec = msec;
  tm.sec = sec;
  tm.min = min;
  tm.hour = hour;
  tm.day = day + 1;
  tm.month = month;
  tm.year = year - 1900;
}

static s64
our_mktime(broken_down_time const & tm)
{
  s64 d;

  d = tm.millisec;
  d += tm.sec * SEC;
  d += tm.min * MIN;
  d += tm.hour * HOUR;
  d += (tm.day - 1) * DAY;

  // add months
  for (int m = 0; m < tm.month; ++m)
    {
      d += MONTHS[m] * DAY;
      if ((m == 1) && (is_leap_year(tm.year)))
        d += DAY;
    }

  int year = tm.year + 1900;
  I(year >= 0);

  // add years (since 1970)
  d += YEAR * (year - 1970);

  // calculate leap days to add (or subtract)
  int add_leap_days = (year - 1) / 4 - 492;
  add_leap_days -= (year - 1) / 100 - 19;
  add_leap_days += (year - 1) / 400 - 4;

  d += DAY * add_leap_days;

  return d;
}

// We might want to consider teaching this routine more time formats.
// gnulib has a rather nice date parser, except that it requires Bison
// (not even just yacc).

date_t
date_t::from_string(string const & d)
{
  try
    {
      size_t i = d.size() - 1;  // last character of the array

      // check the first character which is not a digit
      while (d.at(i) >= '0' && d.at(i) <= '9')
        i--;

      // ignore fractional seconds, if present, or go back to the end of the
      // date string to parse the digits for seconds.
      if (d.at(i) == '.')
        i--;
      else
        i = d.size() - 1;

      // seconds
      u8 sec;
      N(d.at(i) >= '0' && d.at(i) <= '9'
        && d.at(i-1) >= '0' && d.at(i-1) <= '5',
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      sec = (d.at(i-1) - '0')*10 + (d.at(i) - '0');
      i -= 2;
      N(sec < 60,
        F("seconds out of range"));

      // optional colon
      if (d.at(i) == ':')
        i--;

      // minutes
      u8 min;
      N(d.at(i) >= '0' && d.at(i) <= '9'
        && d.at(i-1) >= '0' && d.at(i-1) <= '5',
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      min = (d.at(i-1) - '0')*10 + (d.at(i) - '0');
      i -= 2;
      N(min < 60,
        F("minutes out of range"));

      // optional colon
      if (d.at(i) == ':')
        i--;

      // hours
      u8 hour;
      N((d.at(i-1) >= '0' && d.at(i-1) <= '1'
         && d.at(i) >= '0' && d.at(i) <= '9')
        || (d.at(i-1) == '2' && d.at(i) >= '0' && d.at(i) <= '3'),
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      hour = (d.at(i-1) - '0')*10 + (d.at(i) - '0');
      i -= 2;
      N(hour < 24,
        F("hour out of range"));

      // We accept 'T' as well as spaces between the date and the time
      N(d.at(i) == 'T' || d.at(i) == ' ',
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      i--;

      // day
      u8 day;
      N(d.at(i-1) >= '0' && d.at(i-1) <= '3'
        && d.at(i) >= '0' && d.at(i) <= '9',
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      day = (d.at(i-1) - '0')*10 + (d.at(i) - '0');
      i -= 2;

      // optional dash
      if (d.at(i) == '-')
        i--;

      // month
      u8 month;
      N(d.at(i-1) >= '0' && d.at(i-1) <= '1'
        && d.at(i) >= '0' && d.at(i) <= '9',
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      month = (d.at(i-1) - '0')*10 + (d.at(i) - '0');
      N(month >= 1 && month <= 12,
        F("month out of range in '%s'") % d);
      i -= 2;

      // optional dash
      if (d.at(i) == '-')
        i--;

      // year
      N(i >= 3,
        F("unrecognized date (monotone only understands ISO 8601 format)"));

      // this counts down through zero and stops when it wraps around
      // (size_t being unsigned)
      u32 year = 0;
      u32 digit = 1;
      while (i < d.size())
        {
          N(d.at(i) >= '0' && d.at(i) <= '9',
            F("unrecognized date (monotone only understands ISO 8601 format)"));
          year += (d.at(i) - '0')*digit;
          i--;
          digit *= 10;
        }

      N(year >= 1970,
        F("date too early (monotone only goes back to 1970-01-01T00:00:00)"));
      N(year <= 9999,
        F("date too late (monotone only goes forward to year 9999)"));

      u8 mdays;
      if (month == 2 && is_leap_year(year))
        mdays = MONTHS[month-1] + 1;
      else
        mdays = MONTHS[month-1];

      N(day >= 1 && day <= mdays,
        F("day out of range for its month in '%s'") % d);

      return date_t(year, month, day, hour, min, sec);
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

  // make sure we are still before year 10,000
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

UNIT_TEST(date, our_mktime)
{

#define OK(x) UNIT_TEST_CHECK(our_mktime(t) == (x))

  broken_down_time t = {0, 0, 0, 0, 1, 0, 70};
  OK(0);

  t.year = 100; /* year 2000 */
  OK(u64_C(946684800000));

  // Make sure our_mktime works for years before 1970 as well.
  t.year = 60;  /* year 1960 */
  OK(-10 * YEAR - 3 * DAY);

  t.year = -331; /* year 1569 */
  OK(-FOUR_HUNDRED_YEARS - YEAR);

  t.year = -330; /* year 1570 */
  OK(-FOUR_HUNDRED_YEARS);

  t.year = -329; /* year 1571 */
  OK(-FOUR_HUNDRED_YEARS + YEAR);

  t.year = -328; /* year 1572 */
  OK(-FOUR_HUNDRED_YEARS + 2 * YEAR);

  t.year = -327; /* year 1573 */
  OK(-FOUR_HUNDRED_YEARS + 3 * YEAR + DAY);

  t.year = -326; /* year 1574 */
  OK(-FOUR_HUNDRED_YEARS + 4 * YEAR + DAY);

  t.year = -730; /* year 1170 */
  OK(-2 * FOUR_HUNDRED_YEARS);

  t.year = -1130; /* year 770 AC */
  OK(-3 * FOUR_HUNDRED_YEARS);

  t.year = -1530; /* year 370 AC */
  OK(-4 * FOUR_HUNDRED_YEARS);

  t.year = -1900; /* year 0 anno Domini */
  OK(-1970 * YEAR - (492 - 19 + 4) * DAY);

  t.year = -1901; /* year 1 BC */
  UNIT_TEST_CHECK_THROW(our_mktime(t), std::logic_error);

#undef OK
}

UNIT_TEST(date, from_string)
{
#define OK(x,y) UNIT_TEST_CHECK(date_t::from_string(x).as_iso_8601_extended() \
                            == (y))
#define NO(x) UNIT_TEST_CHECK_THROW(date_t::from_string(x), informative_failure)

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
  NO("120070301T184113");   // equals 12007-03-01T18:41:13

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

  // components out of range
  NO("1969-03-01T18:41:13");

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
#define OK(x,y) do {                                      \
    string s_ = date_t(u64_C(x)).as_iso_8601_extended();  \
    L(FL("date_t: %lu -> %s") % u64_C(x) % s_);           \
    UNIT_TEST_CHECK(s_ == (y));                           \
  } while (0)

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

  // year 9999 limit
  OK(253402300799000, "9999-12-31T23:59:59");
  UNIT_TEST_CHECK_THROW(date_t(u64_C(253402300800000)), std::logic_error);

#undef OK
}

UNIT_TEST(date, comparisons)
{
  date_t may = date_t::from_string("2000-05-01T00:00:00"),
         jun = date_t::from_string("2000-06-01T00:00:00"),
         jul = date_t::from_string("2000-07-01T00:00:00"),
         v;

  // testing all comparisons operators
  UNIT_TEST_CHECK(may < jun);
  UNIT_TEST_CHECK(jun < jul);
  UNIT_TEST_CHECK(may < jul);

  UNIT_TEST_CHECK(jul > may);

  UNIT_TEST_CHECK(may == date_t::from_string("2000-05-01T00:00:00"));
  UNIT_TEST_CHECK(may != date_t::from_string("2000-05-01T00:00:01"));
  UNIT_TEST_CHECK(may != date_t::from_string("2000-09-01T00:00:00"));
  UNIT_TEST_CHECK(may != date_t::from_string("1999-05-01T00:00:00"));

  v = may;
  v += DAY * 31;
  UNIT_TEST_CHECK(v == jun);

  v = jul;
  v -= DAY * 30;
  UNIT_TEST_CHECK(v == jun);

  // check limits for subtractions
  v = date_t(12345000);
  v -= 12345000;
  UNIT_TEST_CHECK(v == date_t::from_string("1970-01-01T00:00:00"));
  UNIT_TEST_CHECK_THROW(v -= 1, std::logic_error);

  // check limits for additions
  v = date_t::from_string("9999-12-31T23:59:00");
  v += 59000;
  UNIT_TEST_CHECK(v == date_t::from_string("9999-12-31T23:59:59"));
  UNIT_TEST_CHECK_THROW(v += 1000, std::logic_error);

  // check date differences
  UNIT_TEST_CHECK(date_t::from_string("2000-05-05T00:00:01") -
                  date_t::from_string("2000-05-05T00:00:00")
                  == 1000);
  UNIT_TEST_CHECK(date_t::from_string("2000-05-05T00:00:01") -
                  date_t::from_string("2000-05-05T00:00:02")
                  == -1000);
  UNIT_TEST_CHECK(date_t::from_string("2000-05-05T01:00:00") -
                  date_t::from_string("2000-05-05T00:00:00")
                  == 3600000);
}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
