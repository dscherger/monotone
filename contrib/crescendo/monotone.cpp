#include "monotone.hh"
#include "monotone_impl.hh"
#include <boost/filesystem/path.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/bind.hpp>
#include <unistd.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <cassert>

//using fs = boost::filesystem;

namespace crescendo {
  namespace monotone {
    class spawn_exception: public std::exception { };
    class bad_read_exception: public std::exception { };
    class bad_write_exception: public std::exception { };
    class bad_version_exception: public std::exception { };
    class bad_format_exception: public std::exception { };
    class out_of_order_exception: public std::exception { };

    /**
     * Open a connection to monotone and return an instance of the interface to it
     *
     * @param db the path to the monotone database
     * @param working the path to the working directory
     */     
    const shared_ptr< monotone > monotone_factory::get_monotone(fs::path db,fs::path working) {
      pid_t pid; 
      fdx_t stdin_pipe[2];
      fdx_t stdout_pipe[2];
      //      fd_t stderr_pipe[2];

      pipe(stdin_pipe);
      pipe(stdout_pipe);
      //      pipe(stderr_pipe); share err for now
      pid=fork();
      if(pid==-1) throw spawn_exception();
      if(pid==0) { 
        // Child

        // close STDIN
        close(0);
    
        // Connect to parent STDIN pipe
        dup(stdin_pipe[0]);
        // Close both pipe end in the array
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        
        // Same again for STDOUT
        close(1);
        dup(stdout_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);

	std::string arg("--db=");
        arg.append(db.native_file_string());
	std::cerr << arg << "\n";
        chdir(working.native_file_string().c_str());
        if(execlp(MONOTONE_EXE.c_str(),MONOTONE_EXE.c_str(),arg.c_str(),"automate","stdio",NULL)==-1) {
          // Gack - we're in a sub-process. Probably should signal the parent
          fprintf(stderr,"FAILED");
	}
	return shared_ptr< monotone >();
      }
      else {
        // Close pipe ends which are now in the child
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        return shared_ptr< monotone >(new monotone_impl(stdin_pipe[1],stdout_pipe[0]));
      }
    }

    /**
     * Format a command name and arguments into the input packet expected
     * by monotone
     *
     * @param cmd the string command name
     * @param args a vector of arguments (not null, but may be empty)
     */ 
    const std::string monotone_impl::make_command(const std::string cmd,std::vector< std::string > args) {
      // All monotone automataion commands start with 'l'
      std::string result("l");
      // Now the command name prefixed by the length and a ':'
      result.append(boost::lexical_cast<std::string>(cmd.size()));
      result.append(":");
      result.append(cmd);
      // Now each argument similarly prefixed
      std::vector< std::string >::iterator iterator=args.begin();
      while(iterator!=args.end()) {
        result.append(boost::lexical_cast<std::string>((*iterator).size()));
        result.append(":");
        result.append(*iterator);
        ++iterator;
      }
      // And finally an 'e' to end the packet and a CR-LF to boot it
      result.append("e\n");
      return result;
    }

    /**
     * Decode the header of a packet in the response from monotone
     *    
     * @param header the struct into which the decoded information is placed
     */
    const void monotone_impl::decode_packet_header(packet_header &header) {
      std::string assemble_buffer;        
      char buf;
      int cread;
      int terminator_count=0;

      // Header format is four ascii fields terminated by ':'
      while(terminator_count<4) {
        cread=read(mtn_stdout,&buf,1);
        if(cread!=1) throw new bad_read_exception();
        if(buf==10) throw new bad_read_exception();
        if(buf==':') { 
          terminator_count++;
          switch(terminator_count) {
	  case 1: 
	    header.command_index=boost::lexical_cast<int>(assemble_buffer);
	    break;
	  case 2:
	    header.error_code=boost::lexical_cast<int>(assemble_buffer);
	    break;
	  case 3: 
	    if(assemble_buffer[0]!='m') header.last=true;
	    break;
	  case 4:
	    header.packet_size=boost::lexical_cast<int>(assemble_buffer);
	    return; 
	  }
          assemble_buffer.clear();
	}
	else assemble_buffer.push_back(buf);
      }
    }

    /**
     * Send a monotone command immediately and read the result.
     * This is an internal method and should not be called from 
     * outside as it will bypass the command queue. Monotone must
     * be idle before calling this method
     *
     * @param cmd the command to send
     * @param args a vector of arguments (not null, but may be empty)
     */
    const std::string monotone_impl::send_immediate(const std::string cmd, const std::vector < std::string > args) {
 
      // Format the command
      std::string cmd_packet=make_command(cmd,args);
  
      // Send it - a single write should be sufficient here
      int cwritten=write(mtn_stdin,cmd_packet.c_str(),cmd_packet.size());
      if(cwritten!=(int)cmd_packet.size()) throw new bad_write_exception();
      
      std::string response;
      read_response(response);
      return response;
    }
    
    /**
     * Read a complete response from monotone.
     * This reads all the packets in a response possibly made up from multiple packets.
     * 
     * @param response the string into which the response is written
     */
    const void monotone_impl::read_response(std::string &response) {
      // Read a reponse possibly made up from multiple packets
      int cmd_index=-1;
      packet_header header;
      do { 
	// Read the header of the response packet
	decode_packet_header(header);
	if(header.error_code!=0) throw new bad_format_exception();
        if(header.packet_size>MONOTONE_MAX_PACKET) throw new bad_format_exception();
        if(cmd_index==-1) cmd_index=header.command_index;
        if(cmd_index!=header.command_index) throw new out_of_order_exception();
	
	// Read the content of the response
	char buffer[header.packet_size+1]; 
	int high_water=0;
	while(high_water<header.packet_size) {
	  int cread=read(mtn_stdout,buffer+high_water,header.packet_size-high_water);
	  if(cread==-1 || cread==0) throw new bad_read_exception();
	  high_water+=cread;
	}
	buffer[high_water]=0; // Force null termination
	response.append(buffer);
      } while(!header.last);
    }
    
    /**
     * Construct a new instance of a monotone interface bound to the
     * monotone automation stream represented by the two file descriptors
     * 
     * @param r_stdin pipe connected to the monotone input
     * @param r_stdout pipe connected to the monotone output
     */
    monotone_impl::monotone_impl(fdx_t r_stdin,fdx_t r_stdout) {
      mtn_stdin=r_stdin;
      mtn_stdout=r_stdout;

      // Check the version number for monotone
      version=send_immediate(MTN_CMD_VERSION,empty_args);
      version=version.substr(0,version.size()-1); // Strip newline
      if(version!=MTN_VERSION) throw new bad_version_exception();

      worker_should_exit=false;
      worker_purge_queue=false;
      worker_busy=false;

      // Start the thread which will manage the IO to monotone
      // We bind to the function do_work on the current instance
      boost::function< void () > worker_fn;
      worker_fn=boost::bind(&monotone_impl::do_work,ref(*this));      
      worker.create_thread(worker_fn);
      
    }

    /**
     * Submit a command to the monotone command queue.
     * This method returns immediately, and callbacks are invoked
     * when the command reaches the head of the queue and starts to execute.
     *
     *
     * @param cmd the command to send
     * @param args a vector of arguments (not null, but may be empty)
     * @param callback the object describing the callback request
     */
    void monotone_impl::queue_command(const monotone_commands cmd,const std::vector < std::string > args, MonotoneCallback callback) {
      assert(callback);
      std::cerr << "QUEUING COMMAND : " << cmd_text[cmd] << "\n";
      work_item work(cmd,make_command(cmd_text[cmd],args),callback);      
      {
	mutex::scoped_lock l(work_queue_mutex);
        while(worker_purge_queue) sleep(100); // TODO: Remove this hack and do it properly
	work_queue.push_back(work);
        work_queue_notify.notify_all();
      }
    }

    /** 
     * Thread to manage communication with monotone
     */
    void monotone_impl::do_work() { 
      std::cerr << "WORKING!\n";
      work_item job;
      
      // Loop until we are told to exit
      while(!worker_should_exit) {
	
	{
          // Lock the queue while we are examining it
	  boost::mutex::scoped_lock queue_guard(work_queue_mutex);
	  
	  // Check for the queue being empty
	  if(work_queue.empty()) {
	    // No work to do. Wait for some. This will yield
	    // the mutex until a call to work_queue_notify.notify
            worker_busy=false;
	    work_queue_notify.wait(queue_guard);
	  }

	  // Check to see if our instruction is to exit
	  if(worker_should_exit) break;
	  
          // Check to see if we have been instructed to purge the queue
	  if(worker_purge_queue) { 
	    work_queue.clear();
	    worker_purge_queue=false;
	    continue;
	  } 
	  
	  // Got some real work to do - pop the next item from the queue
	  // and execute it
	  worker_busy=true;
	  job=work_queue.front();
	  work_queue.erase(work_queue.begin());
	  
	  // Now release the lock as we are done examining the queue
	}
	
        try {
          dispatch_job(job);
	}
	catch(std::exception e) {
	  // TODO: Better error handling here
	  std::cerr << "WORKER: Exception\n";
	}
      }
      std::cerr << "WORKER STOPPED\n";
    }   

    /**
     * Actually send a command to the monotone instance and handle the
     * response
     */    
    void monotone_impl::dispatch_job(const work_item &work) {
      std::cerr << "DISPATCHING JOB " << work.get_raw_command() << "\n";

      // Tell the callback we have started the command
      work.get_callback()->command_started();

      // Send the command
      std::string raw_cmd=work.get_raw_command();
      int cwritten=write(mtn_stdin,raw_cmd.c_str(),raw_cmd.size());
      if(cwritten!=(int)raw_cmd.size()) throw new bad_write_exception();      

      // Read a reponse possibly made up from multiple packets
      // For each stanza in the response, invoke the callback
      std::string response;
      int cmd_index=-1;
      packet_header header;
      do { 
	// Read the header of the response packet
	decode_packet_header(header);

	if(header.error_code!=0)  {
          if(header.error_code==1) throw new bad_format_exception();
          // OK, we have an error message in the stream. Drain it to a 
	  // buffer then dispatch to the error handler
          char buffer[256];
          int cread=0;
	  std::string error_msg;
          while((cread=read(mtn_stdout,buffer,255))>0) {
	    buffer[cread]=0;
	    error_msg.append(buffer);
	  }
          work.get_callback()->command_error(error_msg);
          return;
	}

        if(header.packet_size>MONOTONE_MAX_PACKET) throw new bad_format_exception();
        if(cmd_index==-1) cmd_index=header.command_index;
        if(cmd_index!=header.command_index) throw new out_of_order_exception();
	
	// Read the content of the response
	char buffer[header.packet_size+1]; 
	int high_water=0;
	while(high_water<header.packet_size) {
	  int cread=read(mtn_stdout,buffer,header.packet_size-high_water);
	  if(cread==-1 || cread==0) throw new bad_read_exception();
	  high_water+=cread;
          buffer[cread]=0; // Force null termination
          response.append(buffer);

          size_t line_index=0;
          while(!response.empty() && (line_index=response.rfind(10))!=response.size()) {
            // At least one CR in the buffer. Try to parse it
            // The parser should remove any buffer contents
            // which it has consumed. If it can't match a valid
            // stanza, then it should just return;
	    // TODO: Implement a proper rigorous lazy parser
            parse(work,response);
	  }

 	}
     
      } while(!header.last);

      // Tell the callback we've finished this command
      work.get_callback()->command_complete();

      std::cerr << "DONE:[" << response << "]\n";
      // Check to see that we sucessfully parsed the entire response
      if(!response.empty()) throw new bad_format_exception(); 
    }

    void monotone_impl::parse(const work_item &work,std::string &response) {
      // I loathe switch, but barring a mad class explosion this 
      // is probably the best? TODO: reconsider this decision
      switch(work.get_cmd()) {
      case cmd_branches: { 
	parse_branches(work,response); break;
      }
      case cmd_erase_ancestors:
      case cmd_parents:
      case cmd_children:
      case cmd_ancestors:
      case cmd_common_ancestors:
      case cmd_descendents:
      case cmd_toposort:
      case cmd_ancestry_difference:
      case cmd_get_base_revision_id:
      case cmd_get_current_revision_id:
      case cmd_heads: {
        parse_revisions(work,response); break;
      }
      case cmd_tags: { 
	parse_tags(work,response); break;
      }
      case cmd_certs: {
	parse_certs(work,response); break;
      }
      case cmd_keys: {
	parse_keys(work,response); break;
      }

      default: throw new bad_format_exception(); // temp hack
      }
    }

      
    void monotone_impl::parse_branches(const work_item &work,std::string &response) {
      int terminator;
      while(!response.empty() && (terminator=response.find(10))!=(int)response.size()) {
	// Copy our branch line (always only one line)
	std::string raw_branch=response.substr(0,terminator);
	// Consume the line
	response.erase(0,terminator+1);
        // Invoke callback
        work.get_callback()->stanza_branch(raw_branch); 
      }
    }
    
    void monotone_impl::parse_revisions(const work_item &work,std::string &response) {
      int terminator;
      while(!response.empty() && (terminator=response.find(10))!=(int)response.size()) {
	// Copy our id line (always only one line)
	std::string raw_id=response.substr(0,terminator);
	// Consume the line
	response.erase(0,terminator+1);
        revision_id id(raw_id);

	// Invoke callback
	std::cerr << raw_id << " {" << id << "}\n";
	work.get_callback()->stanza_revision_id(id);
      }
    }
    
    void monotone_impl::branches(MonotoneCallback callback) {
      assert(callback);
      queue_command(cmd_branches, empty_args, callback);
    }

    void monotone_impl::heads(const std::string &branch,MonotoneCallback callback) {
      assert(callback);
      stringv args;
      args.push_back(branch);
      queue_command(cmd_heads, args, callback);
    }

    void monotone_impl::leaves(MonotoneCallback callback) {
      assert(callback);
      queue_command(cmd_heads, empty_args, callback);
    }

    void monotone_impl::get_base_revision_id(MonotoneCallback callback) {
      assert(callback);
      queue_command(cmd_get_base_revision_id, empty_args, callback);
    }

    void monotone_impl::get_current_revision_id(MonotoneCallback callback) {
      assert(callback);
      queue_command(cmd_get_current_revision_id, empty_args, callback);
    }

    void monotone_impl::ancestors(const revision_id_list &id,MonotoneCallback callback) {
      assert(callback);
      assert(id.size()>0);
      stringv args;
      revision_id_list::const_iterator iterator=id.begin();
      revision_id_list::const_iterator end=id.end();
      while(iterator!=end) {
	args.push_back((*iterator).inner()()); 
	++iterator;
      }
      queue_command(cmd_ancestors, args, callback);
    }

    void monotone_impl::common_ancestors(const revision_id_list &id,MonotoneCallback callback) {
      assert(callback);
      assert(id.size()>0);
      stringv args;
      revision_id_list::const_iterator iterator=id.begin();
      revision_id_list::const_iterator end=id.end();
      while(iterator!=end) {
	args.push_back((*iterator).inner()());
	++iterator;
      }
      queue_command(cmd_common_ancestors, args, callback);
    }

    void monotone_impl::parents(const revision_id &id,MonotoneCallback callback) {
      assert(callback);
      stringv args;
      args.push_back(id.inner()());
      queue_command(cmd_parents, args, callback);
    }

    void monotone_impl::tags(const std::string &pattern,MonotoneCallback callback) {
      assert(callback);
      stringv args;
      args.push_back(pattern);
      queue_command(cmd_tags, args, callback);
    }

    void monotone_impl::certs(const revision_id &id,MonotoneCallback callback) {
      assert(callback);
      stringv args;
      args.push_back(id.inner()());
      queue_command(cmd_certs, args, callback);
    }

    void monotone_impl::keys(MonotoneCallback callback) {
      assert(callback);
      queue_command(cmd_keys, empty_args, callback);
    }

    void monotone_impl::descendents(const revision_id_list &id,MonotoneCallback callback) {
      assert(callback);
      assert(id.size()>0);
      stringv args;
      revision_id_list::const_iterator iterator=id.begin();
      revision_id_list::const_iterator end=id.end();
      while(iterator!=end) {
	args.push_back((*iterator).inner()()); 
	++iterator;
      }
      queue_command(cmd_descendents, args, callback);
    }

    void monotone_impl::children(const revision_id &id,MonotoneCallback callback) {
      assert(callback);
      stringv args;
      args.push_back(id.inner()());
      queue_command(cmd_children, args, callback);
    }

    void monotone_impl::erase_ancestors(const revision_id_list &id,MonotoneCallback callback) {
      assert(callback);
      assert(id.size()>0);
      stringv args;
      revision_id_list::const_iterator iterator=id.begin();
      revision_id_list::const_iterator end=id.end();
      while(iterator!=end) {
	args.push_back((*iterator).inner()()); 
	++iterator;
      }
      queue_command(cmd_erase_ancestors, args, callback);
    }

    void monotone_impl::toposort(const revision_id_list &id,MonotoneCallback callback) {
      assert(callback);
      assert(id.size()>0);
      stringv args;
      revision_id_list::const_iterator iterator=id.begin();
      revision_id_list::const_iterator end=id.end();
      while(iterator!=end) {
	args.push_back((*iterator).inner()()); 
	++iterator;
      }
      queue_command(cmd_toposort, args, callback);
    }

    void monotone_impl::ancestry_difference(const revision_id &new_id,const revision_id_list &old_id,MonotoneCallback callback) {
      assert(callback);
      stringv args;
      args.push_back(new_id.inner()());
      revision_id_list::const_iterator iterator=old_id.begin();
      revision_id_list::const_iterator end=old_id.end();
      while(iterator!=end) {
	args.push_back((*iterator).inner()()); 
	++iterator;
      }
      queue_command(cmd_ancestry_difference, args, callback);
    }

    void sink_whitespace(std::string &data) {
      while(data[0]==' ') data.erase(0,1);
    }

    bool count_four_lines(std::string &data) {
      int line_count=0;
      size_t cursor=0;
      while(line_count<4 && cursor<data.size()) {
	cursor=data.find('\n',cursor);
	if(cursor==data.size()) return false; 
        line_count++;
      }
      return true;
      }

    bool count_five_lines(std::string &data) {
      int line_count=0;
      size_t cursor=0;
      while(line_count<5 && cursor<data.size()) {
	cursor=data.find('\n',cursor);
	if(cursor==data.size()) return false; 
        line_count++;
      }
      return true;
      }

    bool count_six_lines(std::string &data) {
      int line_count=0;
      size_t cursor=0;
      while(line_count<6 && cursor<data.size()) {
	cursor=data.find('\n',cursor);
	if(cursor==data.size()) return false; 
        line_count++;
      }
      return true;
      }

    std::string parse_quoted_string(std::string &response) {
      if(response[0]!='\"') throw new bad_format_exception();
      response.erase(0,1); // Opening quote
      std::string result;
      std::string::const_iterator scanner=response.begin();
      std::string::const_iterator end=response.end();

      int quoted=false;
      while(scanner!=end) {
	if(quoted) {
	  quoted=false;
	  result.push_back(*scanner);
	}
	else if(*scanner=='\\') quoted=true;
	else if(*scanner=='\"') {
	  response.erase(0,result.size()+1);
	  return result;
	}
	else result.push_back(*scanner);
	++scanner;
      }
      throw new bad_format_exception();
    }

    std::string parse_quoted_hash(std::string &response) {
      if(response[0]!='[') throw new bad_format_exception();
      response.erase(0,1); // Opening quote
      std::string result;
      std::string::const_iterator scanner=response.begin();
      std::string::const_iterator end=response.end();

      while(scanner!=end) {
	if(*scanner==']') {
	  response.erase(0,result.size()+1);
	  return result;
	}
	else result.push_back(*scanner);
	++scanner;
      }
      throw new bad_format_exception();
    }

      
    std::string parse_tags_tag(std::string &response) {
      sink_whitespace(response);
      if(response.find("tag")!=0) throw new bad_format_exception();
      response.erase(0,4); // consume tag "
      
      std::string tag=parse_quoted_string(response);
      response.erase(0,1); // LF
      return tag;
    }

    revision_id parse_tags_revision(std::string &response) {      
      if(response.find("revision")!=0) throw new bad_format_exception();
      response.erase(0,9);
      revision_id rev(parse_quoted_hash(response));
      response.erase(0,1); // LF
      return rev;
    }

    std::string parse_tags_signer(std::string &response) {
      std::string signer;
      sink_whitespace(response);
      if(response.find("signer")!=0) throw new bad_format_exception(); 
      response.erase(0,7);
      std::string sig=parse_quoted_string(response);
      response.erase(0,1); // LF
      return sig;
    }

    void parse_quoted_list(std::string &response,stringv &results) {
      if(response[0]!='\"') throw new bad_format_exception();
      do {
	results.push_back(parse_quoted_string(response));
	if(response[0]==' ') response.erase(0,1);
      } while(response[0]=='\"');
    }

    void parse_tags_branches(std::string &response,stringv &branches) {
 	if(response.find("branches")!=0) {
	  throw new bad_format_exception();
	}
	response.erase(0,8);
	if(response[0]==' ') response.erase(0,1);
	if(response[0]=='\n') { 
	  response.erase(0,1); 
	  return; // Empty list
	}
	parse_quoted_list(response,branches);
	response.erase(0,1); // LF
	return;	
    }

    void monotone_impl::parse_tags(const work_item &work,std::string &response) {
      if(response.find("format_version \"1\"\n")==0) {
	// valid response line
	// consume it and return
	response.erase(0,20);
	if(response.empty()) return; // More data needed
      }
      
      sink_whitespace(response);
      if(response.find("tag")==0) {
        if(!count_five_lines(response)) return; // More data needed
        // Tag stanza
	std::string tag_name=parse_tags_tag(response);
	revision_id revision=parse_tags_revision(response);
        std::string signer=parse_tags_signer(response);
	stringv branches;
	parse_tags_branches(response,branches);
	response.erase(0,1); // LF
	tag the_stanza(tag_name,revision,signer,branches);
        work.get_callback()->stanza_tag(the_stanza);
	return;
      }
      // More data needed
    }

    std::string parse_certs_key(std::string &response) {
      sink_whitespace(response);
      if(response.find("key")!=0) throw new bad_format_exception();
      response.erase(0,4);
      std::string key=parse_quoted_string(response);
      response.erase(0,1); // LF
      return key;
    }

    std::string parse_certs_sig(std::string &response) {
      if(response.find("signature")!=0) throw new bad_format_exception();
      response.erase(0,10);
      std::string sig=parse_quoted_string(response);
      response.erase(0,1); // LF
      return sig;
    }

    std::string parse_certs_value(std::string &response) {
      sink_whitespace(response);
      if(response.find("value")!=0) throw new bad_format_exception();
      response.erase(0,6);
      std::string val=parse_quoted_string(response);
      response.erase(0,1); // LF
      return val;
    }

    std::string parse_certs_name(std::string &response) {
      sink_whitespace(response);
      if(response.find("name")!=0) throw new bad_format_exception();
      response.erase(0,5);
      std::string name=parse_quoted_string(response);
      response.erase(0,1); // LF
      return name;
    }

    std::string parse_keys_name(std::string &response) {
      sink_whitespace(response);
      if(response.find("name")!=0) throw new bad_format_exception();
      response.erase(0,5);
      std::string name=parse_quoted_string(response);
      response.erase(0,1); // LF
      return name;
    }

    std::string parse_certs_trust(std::string &response) {
      sink_whitespace(response);
      if(response.find("trust")!=0) throw new bad_format_exception();
      response.erase(0,6);
      std::string trust=parse_quoted_string(response);
      response.erase(0,1); // LF
      return trust;
    }

    void monotone_impl::parse_certs(const work_item &work,std::string &response) {
      sink_whitespace(response);
      if(response.find("key")==0) {
        if(!count_six_lines(response)) return; // More data needed
        // Cert stanza
	std::string key=parse_certs_key(response);
	std::string signature=parse_certs_sig(response);
	std::string name=parse_certs_name(response);
	std::string value=parse_certs_value(response);
	std::string trust=parse_certs_trust(response);
	response.erase(0,1); // LF

	cert the_stanza(key,signature,name,value,trust);
        work.get_callback()->stanza_cert(the_stanza);
	return;
      }
      // More data needed
      
    }

    std::string parse_keys_public_hash(std::string &response) {
      sink_whitespace(response);
      if(response.find("public_hash")!=0) throw new bad_format_exception();
      response.erase(0,12);
      std::string hash=parse_quoted_hash(response);
      response.erase(0,1); // LF
      return hash;
    }

    std::string parse_keys_private_hash(std::string &response) {
      sink_whitespace(response);
      if(response.find("private_hash")!=0) throw new bad_format_exception();
      response.erase(0,13);
      std::string hash=parse_quoted_hash(response);
      response.erase(0,1); // LF
      return hash;
    }

    void parse_keys_private_location(std::string &response,stringv &locs) {
      sink_whitespace(response);
      if(response.find("private_location")!=0) throw new bad_format_exception();
      response.erase(0,17);
      parse_quoted_list(response,locs);
      response.erase(0,1); // LF
      return;
    }

    void parse_keys_public_location(std::string &response,stringv &locs) {
      sink_whitespace(response);
      if(response.find("public_location")!=0) throw new bad_format_exception();
      response.erase(0,16);
      parse_quoted_list(response,locs);
      response.erase(0,1); // LF
      return;
    }
    
    void monotone_impl::parse_keys(const work_item &work,std::string &response) {
      sink_whitespace(response);
      if(response.find("name")==0) {
	if(!count_six_lines(response) &&
	   !count_five_lines(response) &&
           !count_four_lines(response)) return; // More data needed 
        // key_info stanza
	std::string key_name=parse_keys_name(response);
	std::string public_hash=parse_keys_public_hash(response);
	std::string private_hash;
	stringv private_location;
	stringv public_location;
	std::cerr << key_name << "\n";
	sink_whitespace(response);
	if(response.find("private_hash")==0) {
	  private_hash.append(parse_keys_private_hash(response));
	}
        parse_keys_public_location(response,public_location);
	if(response.find("private_location")==0) {
	  parse_keys_private_location(response,private_location);
	}
	response.erase(0,1); // LF
	key_info the_stanza(key_name,public_hash,private_hash,public_location,private_location);
        work.get_callback()->stanza_key(the_stanza);
	return;
      }
      // More data needed
    }

  }
}

  
