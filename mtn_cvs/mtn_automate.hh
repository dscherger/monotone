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
{ typedef std::map<file_path,file_id> manifest; // (directories have a null file_id)
  struct cset // make this more like a mtn cset? (split_paths)
  { std::set<file_path> deleted;
    std::map<file_path,file_id> added;
    std::set<file_path> dirs_added;
    std::map<file_path,std::pair<file_id,file_id> > changed;
    // renames, attrs_cleared, set

    bool is_nontrivial() const 
    { return !deleted.empty() || !added.empty() || !changed.empty()
          || !dirs_added.empty(); 
    }
  };
  struct certificate
  { std::string key, name, value;
    enum sigvalid { ok, bad, unknown } signature;
    bool trusted; 
    certificate() : signature(unknown), trusted() {}
  };

  void check_interface_revision(std::string const&minimum);
  revision_id find_newest_sync(std::string const& domain, std::string const& branch="");
  std::string get_sync_info(revision_id const& rid, std::string const& domain);
  file_id put_file(data const& d, file_id const& base=file_id());
  manifest get_manifest_of(revision_id const& rid);
  revision_id put_revision(revision_id const& parent, cset const& changes);
  void cert_revision(revision_id const& rid, std::string const& name, std::string const& value);
  std::vector<certificate> get_revision_certs(revision_id const& rid);
  std::string get_file(file_id const& fid);
  std::vector<revision_id> get_revision_children(revision_id const& rid);
  std::vector<revision_id> get_revision_parents(revision_id const& rid);
};

#endif
