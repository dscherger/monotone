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
struct cert;

void
revision_header(revision_id const rid, revision_t const & rev,
                std::string const & author, date_t const date,
                branch_name const & branch, utf8 const & changelog,
                std::string const & date_fmt, utf8 & header);

void
revision_header(revision_id const rid, revision_t const & rev,
                std::vector<cert> const & certs, std::string const & date_fmt,
                utf8 & header);

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
