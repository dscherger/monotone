#ifndef __UPDATE_HH__
#define __UPDATE_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <vector>
#include <string>

#include "app_state.hh"
#include "manifest.hh"
#include "vocab.hh"

// this function just encapsulates the (somewhat complex) logic 
// behind picking an update target. the actual updating takes
// place in commands.cc, along with most other file-modifying
// actions.

void pick_update_target(manifest_id const & base_ident,
			vector<string> const & sort_certs,
			app_state & app,
			manifest_id & chosen);

#endif // __UPDATE_HH__
