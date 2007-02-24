#ifndef H_MONOTONE_IMPL_H
#define H_MONOTONE_IMPL_H

#include "monotone.hh"
#include <boost/function.hpp>

typedef int fdx_t;

namespace crescendo {
  namespace monotone {

    static const std::string MONOTONE_EXE="mtn";
    static const std::string MTN_CMD_VERSION="interface_version";
    static const std::string cmd_text[] = { 
      "branches",
      "heads",
      "ancestors",
      "common_ancestors",
      "parents",
      "descendents",
      "children",
      "erase_ancestors",
      "toposort",
      "ancestry_difference",
      "leaves",
      "get_base_revision_id",
      "get_current_revision_id",
      "tags",
      "certs",
      "keys"
    };

    // The version of the interface which we are expecting
    static const std::string MTN_VERSION="4.0";

    enum monotone_commands {
      cmd_heads=1,
      cmd_ancestors=2,
      cmd_common_ancestors=3,
      cmd_parents=4,
      cmd_descendents=5,
      cmd_children=6,
      cmd_graph,
      cmd_erase_ancestors=7,
      cmd_toposort=8,
      cmd_ancestry_difference=9,
      cmd_leaves=10,
      cmd_branches=0,
      cmd_tags=13,
      cmd_select,
      cmd_inventory,
      cmd_certs=14,
      cmd_stdio,
      cmd_get_revision,
      cmd_get_base_revision_id=11,
      cmd_get_current_revision_id=12,
      cmd_get_manifest_of,
      cmd_attributes,
      cmd_content_diff,
      cmd_get_file,
      cmd_get_file_of,
      cmd_get_option,
      cmd_keys=15,
      cmd_get_corresponding_path
    };

    /**
     * One command on the queue
     */
    class work_item {

    private:
      monotone_commands cmd;
      std::string command;
      std::string command_options;
      MonotoneCallback callback;

    public: 
      work_item() { };
      work_item(const work_item &other) {
        cmd=other.cmd;
        command=other.command;
        command_options=other.command_options;
        callback=other.callback;
      };
      work_item(const monotone_commands r_cmd,const std::string &r_command,MonotoneCallback r_callback): cmd(r_cmd),command(r_command) { callback=r_callback; }
      work_item(const monotone_commands r_cmd,const std::string &r_command,const std::string &r_options, MonotoneCallback r_callback): cmd(r_cmd),command(r_command),command_options(r_options) { callback=r_callback; }

      const std::string get_raw_command() const { return command; } ;
      const std::string get_command_options() const { return command_options; };
      const MonotoneCallback get_callback() const { return callback; }
      const monotone_commands get_cmd() const { return cmd; }
    };


    class monotone_impl: public monotone {

    private:
      /**
       * Monotone automate response packet header
       */
      struct packet_header {
	int command_index;
	int error_code;
	bool last;
	int packet_size;
      };
      
      fdx_t mtn_stdin;
      fdx_t mtn_stdout;

      // Version of the monotone automation interface
      std::string version;

      // Thread to manage communication with monotone
      // We use a thread_group here for convenience, since thread is
      // not copyable this makes initialisation easier
      boost::thread_group worker;

      // Queue of work pending for the worker thread
      std::vector< work_item > work_queue;

      // Lock for the work queue
      boost::mutex work_queue_mutex;

      // Condition which is used to synchronise work between the worker
      // thread and the boss threads
      boost::condition work_queue_notify;

      // Special request flag to shutdown the worker
      bool worker_should_exit;

      // special request flag to purge the queue after
      // the current command completes
      bool worker_purge_queue;

      // flag which indicates if the worker is currently processing 
      // a command
      bool worker_busy;
      
      // Thread entry point for the worker thread
      void do_work();
      

      const std::string make_command(const std::string cmd,const std::vector< std::string> args);
      const std::string send_immediate(const std::string cmd,const std::vector< std::string> args);
      void queue_command(const monotone_commands cmd,const std::vector< std::string > args, MonotoneCallback callback);
      const void decode_packet_header(packet_header& header);
      const void read_response(std::string &response);
      void dispatch_job(const work_item &job);
      void parse(const work_item &job,std::string &reponse);
      void parse_branches(const work_item &job,std::string &response);
      void parse_revisions(const work_item &job,std::string &response);
      void parse_tags(const work_item &job,std::string &response);
      void parse_certs(const work_item &job,std::string &response);
      void parse_keys(const work_item &job,std::string &response);
      
    public: 
      static const int MONOTONE_MAX_PACKET=16383; // TODO: Verify this

      /**
       * Destructor - shutdown and close communicaton with montone
       * after current command completes
       */
      virtual ~monotone_impl() {
        close_monotone();
      }

      /**
       * Close monotone after the current command completes, and
       * wait for it to exit
       */
      virtual void close_monotone() {
	{ 
	  // Signal the worker thread to finish
	  boost::mutex::scoped_lock queue_guard(work_queue_mutex);
	  worker_should_exit=true;
	  work_queue_notify.notify_all();                
	}
	// Wait for the worker thread to finish
        worker.join_all();
        close(mtn_stdout);
        close(mtn_stdin);         
      }

      /**
       * Clear all pending commands from the queue after the 
       * current command completes. Any attempts to push additional
       * commands will busy-wait until the queue is clear
       */
      virtual void purge_queue() {
	boost::mutex::scoped_lock queue_guard(work_queue_mutex);
        worker_purge_queue=true;
        work_queue_notify.notify_all();        
      }

      monotone_impl(fdx_t r_stdin,fdx_t r_stdout);

      /**
       * Get the version of the monotone automate interface
       */
      const std::string get_version() const { return version; };

      /**
       * List all the branches in the database
       */
      virtual void branches(MonotoneCallback callback);
      virtual void heads(const std::string &branch,MonotoneCallback callback);
      virtual void ancestors(const revision_id_list &id,MonotoneCallback callback);
      virtual void common_ancestors(const revision_id_list &id,MonotoneCallback callback);            
      virtual void parents(const revision_id &id,MonotoneCallback callback); 
      virtual void descendents(const revision_id_list &id,MonotoneCallback callback);                
      virtual void children(const revision_id &id,MonotoneCallback callback);            
	 /*
	 virtual const shared_ptr< revision_graph > graph(MonotoneCallback callback);
	 */
	 virtual void erase_ancestors(const revision_id_list &id, MonotoneCallback callback);
	 virtual void toposort(const revision_id_list &id,MonotoneCallback callback);

	 virtual void ancestry_difference(const revision_id &new_id,const revision_id_list &old_id,MonotoneCallback backcallback);
	 virtual void leaves(MonotoneCallback callback);

	 virtual void tags(const std::string &pattern,MonotoneCallback callback);
	 virtual void certs(const revision_id &id, MonotoneCallback callback);
	 virtual void keys(MonotoneCallback callback);

      /*
	 virtual void select(revision_id_list &results,const selector &selector,MonotoneCallback callback);
	 virtual void inventory(status_list &results,MonotoneCallback callback);

	 virtual const shared_ptr< const revision > get_revision(const revision_id &id,MonotoneCallback callback);      
      */
	 virtual void get_base_revision_id(MonotoneCallback callback);
	 virtual void get_current_revision_id(MonotoneCallback callback);
      /*
	 virtual const shared_ptr< const manifest> get_manifest_of(const revision_id &id,MonotoneCallback callback);
	 virtual void attributes(file_attribute_list &results,const file_id &file,MonotoneCallback callback);
	 virtual const shared_ptr< const content_difference > content_diff(const revision_id &first, const revision_id &second, const file_list &files,MonotoneCallback callback);
	 virtual std::istream get_file(const file_id &file_id,MonotoneCallback callback);
	 virtual std::istream get_file_of(const file_id &file_id,const revision_id &id);
	 virtual std::string get_option(const std::string &name,MonotoneCallback callback);
	 virtual void get_content_changed(revision_id_list &results,const revision_id &id,const file_id &file_id,MonotoneCallback callback);
	 virtual const fs::path get_corresponding_path(const revision_id &source,const fs::path &path, const revision_id &target,MonotoneCallback callback);*/
    };
  }
}

#endif
