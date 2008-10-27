// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef DATES_HH
#define DATES_HH

// This file provides a straightforward wrapper class around the standard
// C time functions.  Note that all operations are done in UTC, *not* the
// user's time zone.

#include "numeric_vocab.hh"
#include "sanity.hh"

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

struct date_t
{
  // initialize to an invalid date
  date_t() : d(-1) {}

  // initialize from a unix timestamp
  date_t(u64 d);

  // Initialize from broken-down time
  date_t(int year, int month, int day,
         int hour=0, int min=0, int sec=0, int millisec=0);

  bool valid() const;

  // Return the local system's idea of the current date.
  static date_t now();

  // Return the date corresponding to a string.  Presently this recognizes
  // only ISO 8601 "basic" and "extended" time formats.
  static date_t from_string(std::string const &);

  // Write out date as a string.
  std::string as_iso_8601_extended() const;

  // Retrieve the internal milliseconds count since the Unix epoch.
  u64 millisecs_since_unix_epoch() const;

  // Date comparison operators
  bool operator <(date_t const & other) const
    { return d < other.d; };
  bool operator <=(date_t const & other) const
    { return d <= other.d; };
  bool operator >(date_t const & other) const
    { return d > other.d; };
  bool operator >=(date_t const & other) const
    { return d >= other.d; };
  bool operator ==(date_t const & other) const
    { return d == other.d; };
  bool operator !=(date_t const & other) const
    { return d != other.d; };

  // Addition and subtraction of millisecond amounts
  date_t & operator +=(s64 const other);
  date_t & operator -=(s64 const other);
  date_t operator +(s64 const other) const;
  date_t operator -(s64 const other) const;

  // Difference between two dates in milliseconds
  s64 operator -(date_t const & other) const;

private:
  // The date as an unsigned 64-bit count of milliseconds since
  // the Unix epoch (1970-01-01T00:00:00.000).
  u64 d;
};

std::ostream & operator<< (std::ostream & o, date_t const & d);
template <> void dump(date_t const & d, std::string & s);

#endif // dates.hh

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
