// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __SELECTORS_HH__
#define __SELECTORS_HH__

#include "vocab.hh"
#include <set>

class options;
class lua_hooks;
class project_t;

// In the normal case, to expand a selector on the command line, use one of
// these functions: the former if the selector can legitimately expand to
// more than one revision, the latter if it shouldn't.  Both treat a
// selector that expands to zero revisions, or a nonexistent revision, as an
// usage error, and generate progress messages when expanding selectors.

void complete(options const & opts, lua_hooks & lua,
              project_t & project, std::string const & str,
              std::set<revision_id> & completions);

void complete(options const & opts, lua_hooks & lua,
              project_t & project, std::string const & str,
              revision_id & completion);

// For extra control, use expand_selector, which is just like the
// first overload of complete() except that it produces no progress messages
// or usage errors.  The only place where this is currently still
// in use is automate select.

void expand_selector(options const & opts, lua_hooks & lua,
                     project_t & project, std::string const & str,
                     std::set<revision_id> & completions);

#endif // __SELECTORS_HH__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
