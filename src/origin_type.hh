// Copyright (C) 2009 Timothy Brownawell  <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __ORIGIN_TYPE_HH__
#define __ORIGIN_TYPE_HH__

// sanity.cc:type_to_string(type t) will need to match this
namespace origin {
  enum type {
    internal,
    network,
    database,
    workspace,
    system,
    user,
    no_fault
  };
}

// Something that knows where it came from.
class origin_aware
{
public:
  origin::type made_from;
  origin_aware() : made_from(origin::internal) {}
  origin_aware(origin::type m) : made_from(m) {}
};

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
