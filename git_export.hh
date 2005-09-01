#ifndef __GIT_EXPORT_HH__
#define __GIT_EXPORT_HH__

// Copyright (C) 2005  Petr Baudis <pasky@suse.cz>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "vocab.hh"
#include "database.hh"

void export_git_repo(system_path const & gitrepo, string const & headname,
                     app_state & app);

#endif // __GIT_EXPORT_HH__
