// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef IGNORE_SET_HH
#define IGNORE_SET_HH

struct ignore_set_impl;
struct file_path;

struct ignore_set
{
  ignore_set_impl * imp;

  ignore_set() : imp(0) {}
  ~ignore_set();

  bool included(file_path const & path);

private:
  ignore_set(ignore_set const &);
  ignore_set & operator=(ignore_set const &);
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
