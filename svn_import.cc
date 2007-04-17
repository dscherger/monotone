// Copyright (C) 2007 Markus Schiltknecht <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "cert.hh"
#include "cmd.hh"
#include "app_state.hh"
#include "keys.hh"
#include "svn_import.hh"

using std::string;

void
import_svn_repo(std::istream const & is, app_state & app)
{
  // early short-circuit to avoid failure after lots of work
  rsa_keypair_id key;
  get_user_key(key, app);
  require_password(key, app);

  N(app.opts.branchname() != "", F("need base --branch argument for importing"));

  string branch = app.opts.branchname();
};
