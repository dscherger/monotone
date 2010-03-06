// Copyright (C) 2010 Derek Scherger <derek@echologic.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __REV_SUMMARY_HH__
#define __REV_SUMMARY_HH__

#include "rev_types.hh"
#include "vocab.hh"

struct date_t;

void
revision_header(revision_id rid, revision_t const & rev, std::string const & author, 
                date_t const date, branch_name const & branch, 
                bool const branch_changed, utf8 & header);

void
revision_summary(revision_t const & rev, utf8 & summary);

#endif  // header guard

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
