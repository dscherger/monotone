// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
// Copyright (C) 2007-2016 Markus Wanner <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <utility>

#include "app_state.hh"

using std::move;

app_state::app_state() : lua(this), mtn_automate_allowed(false)
{}

app_state::~app_state()
{}

void
app_state::push_opt_applicator(options_applicator && applicator)
{
  opt_applicators.push_back(move(applicator));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
