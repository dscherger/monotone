// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <boost/shared_ptr.hpp>

#include <map>

#include "app_state.hh"
#include "database.hh"
#include "lazy_rng.hh"

using boost::shared_ptr;

class app_state_private
{
public:
  std::map<system_path, boost::shared_ptr<database_impl> > databases;
};

app_state::app_state()
  : _hidden(new app_state_private()), lua(this), mtn_automate_allowed(false)
#if BOTAN_VERSION_CODE >= BOTAN_VERSION_CODE_FOR(1,7,7)
  , rng(new lazy_rng())
#endif
{
}

app_state::~app_state()
{}

boost::shared_ptr<database_impl> &
app_state::lookup_db(system_path const & f)
{
  return _hidden->databases[f];
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
