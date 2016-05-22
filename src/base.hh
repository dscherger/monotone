// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __BASE_HH__
#define __BASE_HH__

// This file contains a small number of inclusions and declarations that
// should be visible to the entire program.  Include it first.

// Configuration directives
#include "config.h"

// autoconf prior to 2.64 doesn't define this
#ifndef PACKAGE_URL
#define PACKAGE_URL "http://www.monotone.ca"
#endif

#define BOOST_DISABLE_THREADS
#define BOOST_SP_DISABLE_THREADS
#define BOOST_MULTI_INDEX_DISABLE_SERIALIZATION

#define ASIO_DISABLE_THREADS

// Undefine this if you do not want to support SQLite versions older
// than 3.3.14.
#define SUPPORT_SQLITE_BEFORE_3003014

// Keep out all the GUI stuff on Windows.
#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#endif

// Cygwin somehow enables strict ansi, which we don't want.
#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif

// Clang doesn't support __float128, but glibc needs it.
#ifdef __clang__
struct __float128;
#endif

#include <iosfwd>
#include <string>  // it would be nice if there were a <stringfwd>

// s32, u64, etc
#include "numeric_vocab.hh"

// this template must be specialized for each type you want to dump
// (or apply MM() to -- see sanity.hh).  there are a few stock dumpers
// in appropriate places.
template <typename T>
void dump(T const &, std::string &)
{
  static_assert(sizeof(T) == 0, "missing specialization for dump");
}

template <> void dump(std::string const & obj, std::string & out);
template <> void dump(char const * const & obj, std::string & out);
template <> void dump(bool const & obj, std::string & out);
template <> void dump(int const & obj, std::string & out);
template <> void dump(unsigned int const & obj, std::string & out);
template <> void dump(long const & obj, std::string & out);
template <> void dump(unsigned long const & obj, std::string & out);
#ifdef USING_LONG_LONG
// I don't think these are standard, so only specialize on them
// if we're actually using them.
template <> void dump(long long const & obj, std::string & out);
template <> void dump(unsigned long long const & obj, std::string & out);
#endif

// NORETURN(void function()); declares a function that will never return
// in the normal fashion. a function that invariably throws an exception
// counts as NORETURN.
#if defined(__GNUC__)
#define NORETURN(x) x __attribute__((noreturn))
#elif defined(_MSC_VER)
#define NORETURN(x) __declspec(noreturn) x
#else
#define NORETURN(x) x
#endif

// SQLite versions before 3.3.14 did not have sqlite_prepare_v2. To support
// those SQLite libraries, we must use the old API.
#ifdef SUPPORT_SQLITE_BEFORE_3003014
#define sqlite3_prepare_v2 sqlite3_prepare
#endif

// i18n goo

#include "gettext.h"

#define _(str) gettext(str)
#define N_(str) gettext_noop(str)

#endif // __BASE_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
