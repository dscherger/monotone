#ifndef __DIFF_PATCH_HH__
#define __DIFF_PATCH_HH__

// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// this file is to contain some stripped down, in-process implementations
// of GNU-diffutils-like things (diff, diff3, maybe patch..)

#include "vector.hh"
#include "vocab.hh"


void make_diff(std::string const & filename1,
               std::string const & filename2,
               file_id const & id1,
               file_id const & id2,
               data const & data1,
               data const & data2,
               std::ostream & ost,
               diff_type type,
               std::string const & pattern);

bool merge3(std::vector<std::string> const & ancestor,
            std::vector<std::string> const & left,
            std::vector<std::string> const & right,
            std::vector<std::string> & merged);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __DIFF_PATCH_HH__
