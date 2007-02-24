#ifndef H_CRESCENDO_H
#define H_CRESCENDO_H

#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include "vocab.hh"
#include <boost/utility.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/thread.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/checked_delete.hpp>

using namespace boost;
namespace fs = boost::filesystem;


namespace crescendo {
  typedef std::vector< std::string > stringv;    
  typedef std::vector< revision_id > revision_id_list;
  typedef std::vector< std::string > branch_list;


  class revision_change {};
  typedef std::vector< shared_ptr < revision_change > > revision_change_list;

  class revision { 
  private:
    revision_id id;
    manifest_id manifest;
    revision_id first_old_revision;
    revision_id second_old_revision;
    revision_change_list change;
    revision(const revision_id &rev_id,const manifest_id &rev_manifest, const revision_id &rev_first_old_revision, const revision_id &rev_second_old_revision, const revision_change_list &rev_change): id(rev_id), manifest(rev_manifest), first_old_revision(rev_first_old_revision), second_old_revision(rev_second_old_revision), change(rev_change) {};

  public:
    revision() {};
    const revision_id &getId() const { return id; }
    const manifest_id &getManifest() const { return manifest; }
    const revision_id &getFirstOldRevision() const { return first_old_revision; }
    const revision_id &getSecondOldRevision() const { return second_old_revision; }
    const revision_change_list &getChanges() const { return change; }
    static shared_ptr< const revision > parse(std::istream source) {
      // TODO: implement
      return shared_ptr< const revision >(new revision());
    }
  };

  class manifest_file {
  private:
    fs::path file;
    file_id id;
  };

  class manifest_dir {
  private:
    fs::path dir;
    file_id id;
  };

  typedef std::vector< manifest_file >  manifest_file_list;
  typedef std::vector< manifest_dir > manifest_dir_list;

  class manifest { 
  private: 
    manifest_id id;
    manifest_file_list files;
    manifest_dir_list dirs;
  };

  class tag { 
  private:
    std::string tag_name;
    revision_id id;
    std::string signer;
    branch_list branches;
  public:
    tag(std::string &r_tag_name,revision_id &r_id,std::string &r_signer,branch_list &r_branches): tag_name(r_tag_name),id(r_id),signer(r_signer),branches(r_branches) {
    }
  };

  typedef std::vector< tag > tag_list;

  enum pre_state { pre_unchanged, pre_deleted, pre_renamed };
   enum post_state { post_unchanged, post_renamed, post_added };
   enum file_state { unknown, patched, unknown_unincluded, ignored_unincluded, missing };
   enum rename { left, right };

   class status {
   private:
     fs::path path;
     enum pre_state pre_state;
     enum post_state post_state;
     enum file_state file_state;
     enum rename rename[2];
   };

  typedef std::vector < status > status_list;

   class cert {
   private: 
     std::string key;
     std::string signature;
     std::string name;
     std::string value;
     std::string trust;
   public:
     cert(std::string &r_key,std::string &r_signature,std::string &r_name,std::string &r_value,std::string &r_trust): key(r_key),signature(r_signature),name(r_name),value(r_value),trust(r_trust) {
     };
       
   };

   typedef std::vector< cert > cert_list;

   typedef std::string selector;

   class attribute {
   private: 
     std::string name; 
     std::string value;
   };

   enum attribute_state { added, dropped, unchanged, changed };

   class file_attribute { 
   private: 
     attribute attribute_value;
     enum attribute_state state;
   };

   typedef std::vector< file_attribute > file_attribute_list;

   class key_info {
   private:
     std::string name;
     std::string public_hash;
     std::string private_hash;
     stringv public_location;   
     stringv private_location;
   public:
     key_info(std::string r_name,std::string &r_public_hash,std::string &r_private_hash,stringv &r_public_location,stringv &r_private_location): name(r_name),public_hash(r_public_hash),private_hash(r_private_hash),public_location(r_public_location),private_location(r_private_location) { }
     
   };

  class content_difference {
  };

   typedef std::vector< key_info > key_info_list;

   typedef adjacency_list< setS /* OutEdgeList */, vecS /* VertexList */, directedS /* Directed */, shared_ptr< const revision >/* VertexProperties */, no_property /* EdgeProperties */> revision_graph;

  typedef std::vector< fs::path > file_list;

}
 
#endif
