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

struct date_t
{
  date_t()      : d(0) {}

  // initialize from a unix timestamp
  date_t(u64 d);

  // initialize from multiple values
  date_t(int sec, int min, int hour, int day, int month, int year);

  bool valid() const { return d != 0; }

  // Return the local system's idea of the current date.
  static date_t now();

  // Return the date corresponding to an unsigned 64-bit count of seconds
  // since the Unix epoch (1970-01-01T00:00:00).
  static date_t from_unix_epoch(u64);

  // Return the date corresponding to a string.  Presently this recognizes
  // only ISO 8601 "basic" and "extended" time formats.
  static date_t from_string(std::string const &);

  // Write out date as a string.
  std::string as_iso_8601_extended() const;

  // Date comparison operators
  bool operator <(struct date_t const & other) const
    { return d < other.d; };
  bool operator <=(struct date_t const & other) const
    { return d <= other.d; };
  bool operator >(struct date_t const & other) const
    { return d > other.d; };
  bool operator >=(struct date_t const & other) const
    { return d >= other.d; };
  bool operator ==(struct date_t const & other) const
    { return d == other.d; };
  bool operator !=(struct date_t const & other) const
    { return d != other.d; };

private:
  // The date as an unsigned 64-bit count of seconds since the Unix epoch
  // (1970-01-01T00:00:00).
  u64 d;

  // Our own gmtime function which converts the internal Unix epoch
  // into a struct tm.
  void gmtime(struct tm & tm) const;
  void mktime(struct tm const & tm);
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
