// Copyright 2005   Timothy Brownawell   <tbrownaw@gmail.com>
// Licensed to the public under the GNU GPL v2, see COPYING

// This is for running monotone commands. There are several predefined
// commands to run, or use 'command' to run any automate command or 'runcmd'
// to run any command line command. Both of these provide the output as an
// std::string . The predefined commands may provide some sort of processed
// output. ('cert' returns a vector<cert>, for example)
// The exit code and stderr are lost.

#ifndef __MONOTONE_HH_
#define __MONOTONE_HH_

#include <vector>
#include <string>
using std::string;
using std::vector;


struct inventory_item
{
  enum state_t {patched, missing, unknown, ignored, none};
  state_t state;
  std::string prename;
  std::string postname;
  inventory_item(): state(none){}
  bool operator==(inventory_item const & x)
  {return state == x.state && prename == x.prename && postname == x.postname;}
};

struct cert
{
  std::string key;
  bool sig;
  std::string name;
  std::string value;
  bool trusted;
};

typedef void(*longwait_callback)();

class monotone
{
  pid_t pid;
  std::string dir;// chdir here before exec
  std::string db;// if not empty, add --db= to argument list for exec
  int from;
  int errfrom;
  int to;

  bool execute(std::vector<std::string> args);
  bool start();
  bool stop();
  bool stopped();
  bool read_header(int & cmdnum, int & err, bool & more, int & size);
  bool read_packet(std::string & out);
  longwait_callback lwcb;
public:
  monotone();
  ~monotone();
  void set_longwait_callback(longwait_callback lc);
  longwait_callback get_longwait_callback(){return lwcb;}
  void set_dir(std::string const & s){dir = (s.empty()?".":s); stop();}
  void set_db(std::string const & s){db = s; stop();}
  std::string get_dir(){return dir;}
  std::string get_db(){return db;}

  // run a command with 'automate stdio'
  std::string command(std::string const & cmd,
                      std::vector<std::string> const & args);
  // run a command from the command line
  void runcmd(std::string const & cmd,
              std::vector<std::string> const & args,
              std::string & out, std::string & err);

  void inventory(std::vector<inventory_item> & out);
  std::vector<cert> certs(std::string const & rev);
  std::vector<std::string> select(std::string const & sel);
  void make_cert(std::string const & rev,
                 std::string const & name,
                 std::string const & value);
  std::string commit(std::vector<std::string> args);
  std::string diff(std::string const & filename);
  std::string diff(std::string const & filename,
                   std::string const & rev1,
                   std::string const & rev2);
  std::string cat(std::string const & filename, std::string const & rev);
  std::string get_revision(std::string const & rev);
  std::string get_manifest(std::string const & rev);
  void add(std::string const & file);
  void drop(std::string const & file);
  void revert(std::string const & file);
  void rename(std::string const & oldname, std::string const & newname);
  bool update(std::vector<std::string> & opts, string & out);
  void update(std::string const & rev, string & out);
  void sync(string & res);
};

#endif
