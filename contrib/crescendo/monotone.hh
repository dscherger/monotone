#ifndef H_MONOTONE_H
#define H_MONOTONE_H

#include <boost/thread.hpp>
#include <string>
#include "vocab.hh"
#include <vector>
#include <boost/graph/adjacency_list.hpp>
#include <boost/smart_ptr.hpp>
#include "crescendo.hh"
#include <iostream>

using namespace boost;

/**
 * Main crescendo namespace 
 */
namespace crescendo {
  
  /**
   * Namespace for monotone specific crescendo classes
   */
  namespace monotone {

    /**
     * Default listener which implements no-op methods for all callback methods.
     * This is the java style of doing this, but has the problem that 
     * implementing additional methods can break existing code. There may be 
     * a more C++ like way of doing this with functors
     * 
     * Note that these methods are called on the worker thread in the 
     * monotone interface. If you don't want threaded goodness, see
     * the adaptor classes in adaptor.hh which are pre-canned
     * callbacks which enable you to be synchronous.
     */
    class monotone_listener {
    public:
      monotone_listener() {};
      virtual ~monotone_listener() { };
      virtual void command_started() { };      
      virtual void raw_data(const std::string &raw_data) { };
      virtual void stanza_revision_id(const revision_id &revision) { };
      virtual void stanza_revision_graph(const revision_id_list &id_list) { };

      virtual void stanza_branch(const std::string &branch) { };
      virtual void stanza_tag(const tag &tag) { };
      virtual void stanza_file_status(const status &status) { };
      virtual void stanza_cert(const cert &cert) { };
   
      virtual void stanza_manifest_dir(const manifest_dir &manifist_dir) { };
      virtual void stanza_manifest_file(const manifest_file &manifest_file) { };
      virtual void stanza_new_manifest(const manifest_id &manifest) { };
      virtual void stanza_old_revision(const revision_id &revision) { };
      virtual void stanza_delete(const fs::path &path) { };
      virtual void stanza_rename(const fs::path &from,const fs::path &to) { };
      virtual void stanza_add_dir(const fs::path &dir) { };
      virtual void stanza_add_file(const fs::path &file) { };
      virtual void stanza_patch(const fs::path &file,const file_id &from,const file_id &to) { };
      virtual void stanza_clear(const fs::path &file, const std::string &name) { };
      virtual void stanza_set(const fs::path &file, const std::string &name, const std::string &value) { };
      virtual void stanza_attribute(const file_attribute &attribute) { };
      virtual void file_contents(std::istream &source) { };
      virtual void stanza_option(const std::string &option) { };
      virtual void stanza_key(const key_info &key) { };
      virtual void stanza_file(const fs::path &file) { };
      virtual void command_complete() { };
      virtual void command_error(const std::string &error) { 
	// Default error handler. Override this if you want to
	// handle errors youself
	std::cerr << "MONOTONE ERROR: " << error << "\n";
	  };
      
    };

    typedef class monotone_listener *MonotoneCallback;
    static const stringv empty_args;

    /** 
     * Protocol class for an interface to monotone. An instance of this
     * class should be obtained from a monotone_factory.
     */
    class monotone {
    
    public: 
      virtual ~monotone() {};

      /**
       * Shutdown this interface to monotone and wait for it to close
       */
      virtual void close_monotone() = 0;

      /**
       * Purge the queue of any pending commands
       */
      virtual void purge_queue() = 0;

      /**
       * Get the version of the interface to monotone. TODO: Should this be here?
       */
      virtual const std::string get_version() const = 0;

      /**
       * Get the list of branches in the current monotone database
       * @param callback a pointer to the callback object for the results
       */
      virtual void branches(MonotoneCallback callback) = 0;

      /**
       * Get the list of heads for the specified branch.
       * Result is a callback for each revision_id which is a head on the branch
       *
       * @param branch the name of a branch in the current monotone database
       * @param callback a pointer to the callback object for the results
       */
      virtual void heads(const std::string &branch,MonotoneCallback callback) = 0;

      /**
       * Get the list of revisions which are ancestors of the specified list of revisions.
       * Result is a callback for each revision_id which is an ancestor of the specified revision ids
       * @param id the list of identifiers for which ancestors should be found
       * @param callback a pointer to the callback object for the results
       */
      virtual void ancestors(const revision_id_list &id,MonotoneCallback callback) = 0;

      /**
       * Get the list of revisions which are common ancestors of the specified list of revisions.
       * Result is a callback for each revision_id which is an ancestor of the specified revision ids
       * @param id the list of identifiers for which ancestors should be found
       * @param callback a pointer to the callback object for the results
       */
      virtual void common_ancestors(const revision_id_list &id,MonotoneCallback callback) = 0;            

      /**
       * Get the list of parents for the specified revision.
       * Result is a callback for each revision_id which is a parent of the revision
       *
       * @param id the revision for which the parents should be returned
       * @param callback a pointer to the callback object for the results
       */
      virtual void parents(const revision_id &id,MonotoneCallback callback) = 0;
                  
      /**
       * Get the list of revisions which are descendents of the specified list of revisions.
       * Result is a callback for each revision_id which is an descendent of the specified revision ids
       * @param id the list of identifiers for which descendents should be found
       * @param callback a pointer to the callback object for the results
       */
      virtual void descendents(const revision_id_list &id,MonotoneCallback callback) = 0;                

      /**
       * Get the list of children for the specified revision.
       * Result is a callback for each revision_id which is a child of the revision
       *
       * @param id the revision for which the children should be returned
       * @param callback a pointer to the callback object for the results
       */
      virtual void children(const revision_id &id,MonotoneCallback callback) = 0;            
      /*
      virtual const shared_ptr< revision_graph > graph(MonotoneCallback callback) = 0;
      */

      /**
       * Get the list of revisions in the input which are not an ancestor of some other revision in the input.
       * Result is a callback for each revision_id which is not an ancestor of another revision in the input list
       * @param id the input list of revisions
       * @param callback a pointer to the callback object for the results
       */
      virtual void erase_ancestors(const revision_id_list &id, MonotoneCallback callback) = 0;
      
      /**
       * Topological sort of the input list.
       * Result is a callback for each revision_id in topological order
       * @param id the input list of revisions
       * @param callback a pointer to the callback object for the results
       */
      virtual void toposort(const revision_id_list &id,MonotoneCallback callback) = 0;
      
      /**
       * Get the list of ancestors for new_id which are not also ancestors of old_id
       * Result is a callback for each revision_id which is an ancestor of new_id but not an ancestor of old_id
       * @param new_id the new revision identifier
       * @param old_id a possibly empty list of old revisions
       * @param callback a pointer to the callback object for the results
       */
      virtual void ancestry_difference(const revision_id &new_id,const revision_id_list &old_id,MonotoneCallback backcallback) = 0;

      /**
       * Get the list of revisions which are leaves of the graph
       * Result is a callback for each revision_id which is a leaf of the graph
       * 
       * @param callback a pointer to the callback object for the results
       */
      virtual void leaves(MonotoneCallback callback) = 0;
      virtual void tags(const std::string &pattern,MonotoneCallback callback) = 0;
      virtual void certs(const revision_id &id, MonotoneCallback callback) = 0;
      virtual void keys(MonotoneCallback callback) = 0;
/*

      virtual void select(revision_id_list &results,const selector &selector,MonotoneCallback callback) = 0;
      virtual void inventory(status_list &results,MonotoneCallback callback) = 0;

      virtual const shared_ptr< const revision > get_revision(const revision_id &id,MonotoneCallback callback) = 0;      

      */

      /*
       * Get the base revision of the workspace
       * Result is a single callback for a revision_id
       *
       * @param callback a pointer to the callback object for the results
       */
      virtual void get_base_revision_id(MonotoneCallback callback) = 0;

      /*
       * Get the current revision of the workspace.
       * The current revision is the revision which would be committed by 
       * and unrestricted commit on the current workspace.
       * Result is a single callback for a revision_id
       *
       * @param callback a pointer to the callback object for the results
       */
      virtual void get_current_revision_id(MonotoneCallback callback) = 0;

      /*
      virtual const shared_ptr< const manifest> get_manifest_of(const revision_id &id,MonotoneCallback callback) = 0;
      virtual void attributes(file_attribute_list &results,const file_id &file,MonotoneCallback callback) = 0;
      virtual const shared_ptr< const content_difference > content_diff(const revision_id &first, const revision_id &second, const file_list &files,MonotoneCallback callback) = 0;
      virtual std::istream get_file(const file_id &file_id,MonotoneCallback callback) = 0;
      virtual std::istream get_file_of(const file_id &file_id,const revision_id &id) = 0;
      virtual std::string get_option(const std::string &name,MonotoneCallback callback) = 0;
      virtual void get_content_changed(revision_id_list &results,const revision_id &id,const file_id &file_id,MonotoneCallback callback) = 0;
      virtual const fs::path get_corresponding_path(const revision_id &source,const fs::path &path, const revision_id &target,MonotoneCallback callback) = 0;*/
    };

    /**
     * Factory class which creates implementations of the monotone protocol
     */
    class monotone_factory {
    public:
      monotone_factory() {};

      /**
       * Open a new connection to monotone and return the protocol interface.
       * @param db the path to the monotone database to use
       * @param working the path to the working directory to use
       */
      const shared_ptr< monotone > get_monotone(fs::path db,fs::path working);
    };
    
  }
}

#endif
