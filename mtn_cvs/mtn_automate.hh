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
{ typedef std::map<attr_key, attr_value> attr_map_t;
  // (directories have a null file_id)
  typedef std::map<file_path,std::pair<file_id,attr_map_t> > manifest_map;

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
  typedef std::map<std::pair<split_path, attr_key>, attr_value> sync_map_t;

// methods
  void check_interface_revision(std::string const&minimum);
  revision_id find_newest_sync(std::string const& domain, std::string const& branch="");
  
  sync_map_t get_sync_info(revision_id const& rid, std::string const& domain);
  void put_sync_info(revision_id const& rid, std::string const& domain, sync_map_t const& data);

  file_id put_file(file_data const& d, file_id const& base=file_id());
  manifest_map get_manifest_of(revision_id const& rid);
  revision_id put_revision(revision_id const& parent, cset const& changes);
  void cert_revision(revision_id const& rid, std::string const& name, std::string const& value);
  std::vector<certificate> get_revision_certs(revision_id const& rid);
  std::vector<certificate> get_revision_certs(revision_id const& rid, cert_name const& name);
  file_data get_file(file_id const& fid);
  std::vector<revision_id> get_revision_children(revision_id const& rid);
  std::vector<revision_id> get_revision_parents(revision_id const& rid);
  revision_t get_revision(revision_id const& rid);
  std::vector<revision_id> heads(std::string const& branch);
  
  std::string get_option(std::string const& name);

private:
  bool is_synchronized(revision_id const& rid, revision_t const& rev, std::string const& domain);
  sync_map_t get_sync_info(revision_id const& rid, std::string const& domain, int &depth);
};

#endif
