#ifndef __GIT_IMPORT_HH__
#define __GIT_IMPORT_HH__

// Copyright (C) 2005  Petr Baudis <pasky@suse.cz>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "vocab.hh"
#include "database.hh"

void import_git_repo(fs::path const & gitrepo, app_state & app);

#endif // __GIT_IMPORT_HH__
