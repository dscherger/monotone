// Copyright (C) 2007-2009 Zack Weinberg <zackw@panix.com>
//                         Markus Wanner <markus@bluegap.ch>
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
//
// Note that while we track dates to the millisecond in memory, we do not
// record milliseconds in the database, nor do we ask the system for
// sub-second resolution when retrieving the current time, nor do we display
// milliseconds to the user.  There isn't much point in fixing one of these
// problems if we don't fix all of them, and while the first two would be
// straightforward, the third is very hard -- it would require us to
// reimplement strftime() with our own extension for the purpose.

// On Solaris, these macros are already defined by system includes. We want
// to use our own, so we undef them here.
#undef SEC
#undef MILLISEC

using std::localtime;
using std::mktime;
using std::numeric_limits;
using std::ostream;
using std::string;
using std::time_t;
using std::tm;

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
our_gmtime(s64 ts, broken_down_time & tb)
{
  // validate our assumptions about which basic type is u64 (see above).
  I(PROBABLE_S64_MAX == numeric_limits<s64>::max());
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

  tb.millisec = ms_in_day % 1000;
  ms_in_day /= 1000;

  tb.sec = ms_in_day % 60;
  ms_in_day /= 60;

  tb.min = ms_in_day % 60;
  tb.hour = ms_in_day / 60;

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

  tb.year = year;
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
  tb.month = month;
  tb.day = days + 1;
}

static s64
our_timegm(broken_down_time const & tb)
{
  s64 d;

  // range checks
  I(tb.year  >  0    && tb.year  <= 292278994);
  I(tb.month >= 1    && tb.month <= 12);
  I(tb.day   >= 1    && tb.day   <= 31);
  I(tb.hour  >= 0    && tb.hour  <= 23);
  I(tb.min   >= 0    && tb.min   <= 59);
  I(tb.sec   >= 0    && tb.sec   <= 60);
  I(tb.millisec >= 0 && tb.millisec <= 999);

  // years (since 1970)
  d = YEAR * (tb.year - 1970);
  // leap days to add (or subtract)
  int add_leap_days = (tb.year - 1) / 4 - 492;
  add_leap_days -= (tb.year - 1) / 100 - 19;
  add_leap_days += (tb.year - 1) / 400 - 4;
  d += add_leap_days * DAY;

  // months
  for (int m = 1; m < tb.month; ++m)
    {
      d += DAYS_PER_MONTH[m-1] * DAY;
      if (m == 2 && is_leap_year(tb.year))
        d += DAY;
    }

  // days within month, and so on
  d += (tb.day - 1) * DAY;
  d += tb.hour * HOUR;
  d += tb.min * MIN;
  d += tb.sec * SEC;

  return MILLISEC(d) + tb.millisec;
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

  time_t epoch = 0;
  tm t = *std::gmtime(&epoch);

  our_t.millisec = 0;
  our_t.sec = t.tm_sec;
  our_t.min = t.tm_min;
  our_t.hour = t.tm_hour;
  our_t.day = t.tm_mday;
  our_t.month = t.tm_mon + 1;
  our_t.year = t.tm_year + 1900;

  epoch_offset = our_timegm(our_t);

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

// WARNING: do not log anything within this function; since this is used in
// user_interface::output_prefix() this might lead to an indefinite loop!
date_t
date_t::now()
{
  time_t t = std::time(0);
  s64 tu = MILLISEC(t) + get_epoch_offset();
  E(valid_ms_count(tu), origin::system,
    F("current date '%s' is outside usable range\n"
      "(your system clock may not be set correctly)")
    % std::ctime(&t));
  return date_t(tu);
}

string
date_t::as_iso_8601_extended() const
{
  broken_down_time tb;
  I(valid());
  our_gmtime(d, tb);
  return (FL("%04u-%02u-%02uT%02u:%02u:%02u")
             % tb.year % tb.month % tb.day
             % tb.hour % tb.min % tb.sec).str();
}

ostream &
operator<< (ostream & o, date_t const & d)
{
  return o << d.as_iso_8601_extended();
}

template <> void
dump(date_t const & d, string & s)
{
  s = d.as_iso_8601_extended();
}

// WARNING: do not log anything within this function; since this is used in
// user_interface::output_prefix() this might lead to an indefinite loop!
string
date_t::as_formatted_localtime(string const & fmt) const
{
  // note that the time_t value here may underflow or overflow if our date
  // is outside of the representable range. for 32 bit time_t's the earliest
  // representable time is 1901-12-13 20:45:52 UTC and the latest
  // representable time is 2038-01-19 03:14:07 UTC. assert that the value is
  // within range for the current time_t type so that localtime doesn't
  // produce a bad result.

  s64 seconds = d/1000 - get_epoch_offset();

  E(seconds >= numeric_limits<time_t>::min(), origin::user,
    F("date '%s' is out of range and cannot be formatted")
    % as_iso_8601_extended());

  E(seconds <= numeric_limits<time_t>::max(), origin::user,
    F("date '%s' is out of range and cannot be formatted")
    % as_iso_8601_extended());

  time_t t(seconds); // seconds since unix epoch in UTC
  tm tb(*localtime(&t)); // converted to local timezone values

  char buf[128];

  // Poison the buffer so we can tell whether strftime() produced
  // no output at all.
  buf[0] = '#';

  size_t wrote = strftime(buf, sizeof buf, fmt.c_str(), &tb);

  if (wrote > 0)
    return string(buf); // yay, it worked

  if (wrote == 0 && buf[0] == '\0') // no output
    {
      static bool warned = false;
      if (!warned)
        {
          warned = true;
          W(F("time format specification '%s' produces no output") % fmt);
        }
      return string();
    }

  E(false, origin::user,
    F("date '%s' is too long when formatted using '%s'"
      " (the result must fit in %d characters)")
    % (sizeof buf - 1));
}

date_t
date_t::from_formatted_localtime(string const & s, string const & fmt)
{
  tm tb;
  memset(&tb, 0, sizeof(tb));
  char *p = strptime(s.c_str(), fmt.c_str(), &tb); // local timezone values

  E(p, origin::user, // strptime failed to match all of the format string
    F("unable to parse date '%s' with format '%s'") % s % fmt);
  
  E(*p == 0, origin::user, // extraneous characters in input string
    F("invalid date '%s' not matched by format '%s'") % s % fmt);

  // note that the time_t value here may underflow or overflow if our date
  // is outside of the representable range. for 32 bit time_t's the earliest
  // representable time is 1901-12-13 20:45:52 UTC and the latest
  // representable time is 2038-01-19 03:14:07 UTC. mktime seems to detect this
  // and return -1 for values it cannot handle.

  time_t t = mktime(&tb); // converted to seconds since unix epoch in UTC

  // -1 is also 1960-12-31 23:59:59 but mktime uses it to indicate errors

  E(t != -1, origin::user,
    F("date '%s' is out of range and cannot be parsed")
    % s);

  tm check(*localtime(&t)); // back to local timezone values

  E(tb.tm_sec   == check.tm_sec &&
    tb.tm_min   == check.tm_min &&
    tb.tm_hour  == check.tm_hour &&
    tb.tm_mday  == check.tm_mday &&
    tb.tm_mon   == check.tm_mon &&
    tb.tm_year  == check.tm_year &&
    tb.tm_wday  == check.tm_wday &&
    tb.tm_yday  == check.tm_yday &&
    tb.tm_isdst == check.tm_isdst,
    origin::user, 
    F("date '%s' is out of range and cannot be parsed")
    % s);

  return date_t(MILLISEC(t) + get_epoch_offset());
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
      E(s.at(i) >= '0' && s.at(i) <= '9'
        && s.at(i-1) >= '0' && s.at(i-1) <= '5', origin::user,
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      sec = (s.at(i-1) - '0')*10 + (s.at(i) - '0');
      i -= 2;
      E(sec <= 60, origin::user,
        F("seconds out of range"));

      // optional colon
      if (s.at(i) == ':')
        i--;

      // minutes
      u8 min;
      E(s.at(i) >= '0' && s.at(i) <= '9'
        && s.at(i-1) >= '0' && s.at(i-1) <= '5', origin::user,
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      min = (s.at(i-1) - '0')*10 + (s.at(i) - '0');
      i -= 2;
      E(min < 60, origin::user,
        F("minutes out of range"));

      // optional colon
      if (s.at(i) == ':')
        i--;

      // hours
      u8 hour;
      E((s.at(i-1) >= '0' && s.at(i-1) <= '1'
         && s.at(i) >= '0' && s.at(i) <= '9')
        || (s.at(i-1) == '2' && s.at(i) >= '0' && s.at(i) <= '3'), origin::user,
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      hour = (s.at(i-1) - '0')*10 + (s.at(i) - '0');
      i -= 2;
      E(hour < 24, origin::user,
        F("hour out of range"));

      // We accept 'T' as well as spaces between the date and the time
      E(s.at(i) == 'T' || s.at(i) == ' ', origin::user,
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      i--;

      // day
      u8 day;
      E(s.at(i-1) >= '0' && s.at(i-1) <= '3'
        && s.at(i) >= '0' && s.at(i) <= '9', origin::user,
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      day = (s.at(i-1) - '0')*10 + (s.at(i) - '0');
      i -= 2;

      // optional dash
      if (s.at(i) == '-')
        i--;

      // month
      u8 month;
      E(s.at(i-1) >= '0' && s.at(i-1) <= '1'
        && s.at(i) >= '0' && s.at(i) <= '9', origin::user,
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      month = (s.at(i-1) - '0')*10 + (s.at(i) - '0');
      E(month >= 1 && month <= 12, origin::user,
        F("month out of range in '%s'") % s);
      i -= 2;

      // optional dash
      if (s.at(i) == '-')
        i--;

      // year
      E(i >= 3, origin::user,
        F("unrecognized date (monotone only understands ISO 8601 format)"));

      // this counts down through zero and stops when it wraps around
      // (size_t being unsigned)
      u32 year = 0;
      u32 digit = 1;
      while (i < s.size())
        {
          E(s.at(i) >= '0' && s.at(i) <= '9', origin::user,
            F("unrecognized date (monotone only understands ISO 8601 format)"));
          year += (s.at(i) - '0')*digit;
          i--;
          digit *= 10;
        }

      E(year >= 1, origin::user,
        F("date too early (monotone only goes back to 0001-01-01T00:00:00)"));
      E(year <= 292278994, origin::user,
        F("date too late (monotone only goes forward to year 292,278,993)"));

      u8 mdays;
      if (month == 2 && is_leap_year(year))
        mdays = DAYS_PER_MONTH[month-1] + 1;
      else
        mdays = DAYS_PER_MONTH[month-1];

      E(day >= 1 && day <= mdays, origin::user,
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
      E(false, origin::user,
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


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
