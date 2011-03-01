// Copyright (C) 2010 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __OPTIONS_APPLICATOR_HH__
#define __OPTIONS_APPLICATOR_HH__

class options;

class options_applicator_impl;
class options_applicator
{
  options_applicator_impl * const _impl;
public:
  enum for_what {for_primary_cmd, for_automate_subcmd};
  options_applicator(options const & opts, for_what what);
  ~options_applicator();
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
