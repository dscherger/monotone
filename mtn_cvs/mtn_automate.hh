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
#include <boost/shared_ptr.hpp>

// frontend
struct mtn_automate : mtn_pipe
{ typedef std::map<file_path,file_id> manifest_map; // (directories have a null file_id)

  struct cset
  { path_set nodes_deleted;
    path_set dirs_added;
    std::map<split_path,file_id> files_added;
    std::map<split_path,split_path> nodes_renamed;
    std::map<split_path,std::pair<file_id,file_id> > deltas_applied;
    std::set<std::pair<split_path, attr_key> > attrs_cleared;
    std::map<std::pair<split_path, attr_key>, attr_value> attrs_set;
    
    bool is_nontrivial() const 
    { return !nodes_deleted.empty() || !files_added.empty() || !deltas_applied.empty()
          || !dirs_added.empty() || !nodes_renamed.empty() || !attrs_cleared.empty()
          || !attrs_set.empty(); 
    }
  };
  typedef std::map<revision_id, boost::shared_ptr<cset> > edge_map;
  struct revision_t
  { edge_map edges;
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
  void put_sync_info(revision_id const& rid, std::string const& domain, std::string const& data);
  file_id put_file(data const& d, file_id const& base=file_id());
  manifest_map get_manifest_of(revision_id const& rid);
  revision_id put_revision(revision_id const& parent, cset const& changes);
  void cert_revision(revision_id const& rid, std::string const& name, std::string const& value);
  std::vector<certificate> get_revision_certs(revision_id const& rid);
  std::string get_file(file_id const& fid);
  std::vector<revision_id> get_revision_children(revision_id const& rid);
  std::vector<revision_id> get_revision_parents(revision_id const& rid);
  revision_t get_revision(revision_id const& rid);
};

#endif
