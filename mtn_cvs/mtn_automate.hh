#ifndef ZE6097FE_35CE_491A_A778_4A888AB86991
#define ZE6097FE_35CE_491A_A778_4A888AB86991

// Copyright (C) 2006 Christof Petig <christof@petig-baender.de>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "mtn_pipe.hh"
#include <vocab.hh>

// frontend
struct mtn_automate : mtn_pipe
{ void check_interface_revision(std::string const&minimum);
  revision_id find_newest_sync(std::string const& domain, std::string const& branch="");
  std::string get_sync_info(revision_id const& rid, std::string const& domain);
  file_id put_file(data const& d, file_id const& base=file_id());
};

#endif
