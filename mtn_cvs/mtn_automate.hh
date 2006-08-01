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
#include <paths.hh>
#include <set>
#include <map>

// frontend
struct mtn_automate : mtn_pipe
{ typedef std::map<file_path,file_id> manifest; // (directories missing)
  struct revision
  { hexenc<id> new_manifest;
    revision_id old_revision;
    std::set<file_path> deleted;
    std::map<file_path,file_id> added;
    std::map<file_path,std::pair<file_id,file_id> > changed;
    
    bool is_nontrivial() const 
    { return !deleted.empty() || !added.empty() || !changed.empty(); }
  };

  void check_interface_revision(std::string const&minimum);
  revision_id find_newest_sync(std::string const& domain, std::string const& branch="");
  std::string get_sync_info(revision_id const& rid, std::string const& domain);
  file_id put_file(data const& d, file_id const& base=file_id());
  manifest get_manifest_of(revision_id const& rid);
  revision_id put_revision(revision const& r); // new manifest is not needed
};

#endif
