#ifndef H_ADAPTOR_H
#define H_ADAPTOR_H


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
     * Adaptor base class used to make synchronous calls to monotone.
     * use the wait_for_completion() method. You should sub-class
     * this class and override the particularly callback you're interested
     * in.
     */
    class monotone_adaptor: public monotone_listener {

    private: 
      /**
       * Command completion mutex
       */
      boost::mutex cmd_mutex;

      /**
       * Command completion notification condition
       */
      boost::condition cmd_notify;

      /**
       * Command completion status flag
       */
      bool complete;

    public:
      /**
       * Construct a new monotone_adaptor
       */
      monotone_adaptor() { 
	complete=false; 
      }

      /**
       * Block current thread until this command has completed.
       */
      void wait_for_completion() {
        boost::mutex::scoped_lock l(cmd_mutex);
        cmd_notify.wait(l);
      }

      /**
       * Callback implementation to signal this command complete.
       * This is called by the monotone worker thread and should
       * not be called by the user.
       */
      virtual void command_complete() {
        boost::mutex::scoped_lock l(cmd_mutex);
        cmd_notify.notify_all();
        complete=true;
      }

      /**
       * Polling method to test if this command is complete.
       */
      bool is_complete() {
        boost::mutex::scoped_lock l(cmd_mutex);
        return complete;
      }         
    };

    /**
     * Monotone callback adaptor for commands which result in a list
     * of revision_id.
     */
    class revision_id_list_adaptor: public monotone_adaptor {

    private:
      /**
       * The list which will be populated with the revision_id from the command result
       */
      revision_id_list list;
    
    public: 
      revision_id_list_adaptor(): list() { }
      virtual ~revision_id_list_adaptor() { }

      /**
       * Callback implementation for a revision_id stanza.
       * This is called by the monotone worker thread and should
       * not be called by the user.
       */
      virtual void stanza_revision_id(const revision_id &revision) {
	list.push_back(revision);
      }

      /**
       * Get the list of revision_id. This should only be called after
       * wait_for_completion() has returned.
       *
       * @return the list of revision_id from the command or an empty list
       */
      const revision_id_list &get_list() const { 
	return list; 
      }
    };
       
    class tag_list_adaptor: public monotone_adaptor {
    private:
      tag_list list;
    
    public: 
      virtual ~tag_list_adaptor() { }
      virtual void stanza_tag(const tag &tag) {
	list.push_back(tag);
      }
      const tag_list &get_list() const { return list; }
    };

    class status_list_adaptor: public monotone_adaptor {
    private:
      status_list list;
    
    public: 
      virtual ~status_list_adaptor() { }
      virtual void stanza_file_status(const status &status) {
	list.push_back(status);
      }
      const status_list &get_list() const { return list; }
     
    };

    class branch_list_adaptor: public monotone_adaptor {
    private:
      branch_list list;
    public:
      branch_list_adaptor(): list() { };
      virtual ~branch_list_adaptor() { };
      virtual void stanza_branch(const std::string &branch) {
	list.push_back(branch);
      }
      const branch_list &get_list() const { return list; }
    };

    class cert_list_adaptor: public monotone_adaptor {
    private:
      cert_list list;
    
    public: 
      virtual ~cert_list_adaptor() { }
      virtual void stanza_cert(const cert &cert) {
	list.push_back(cert);
      }
      const cert_list &get_list() const { return list; }
    };

    class key_info_list_adaptor: public monotone_adaptor {
    private:
      key_info_list list;
    
    public: 
      virtual ~key_info_list_adaptor() { }
      virtual void stanza_key_info(const key_info &key_info) {
	list.push_back(key_info);
      }
      const key_info_list &get_list() const { return list; }
    };

  }
}

#endif
  
