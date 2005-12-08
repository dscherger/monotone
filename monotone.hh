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

#include <gtkmm.h>
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
  string key;
  bool sig;
  string name;
  string value;
  bool trusted;
};

class monotone
{
  enum Mode {STDIO, EXEC};
  Mode mode;
  Glib::Pid pid;
  string dir;// chdir here before exec
  string db;// if not empty, add --db= to argument list for exec
  int from;
  int errfrom;
  int to;
  bool busy;

  bool execute(vector<string> args);
  bool start();
  bool stop();
  bool stopped();

  void child_exited(Glib::Pid p, int c);
  void setup_callbacks();
  bool got_data(Glib::IOCondition c, Glib::RefPtr<Glib::IOChannel> chan);
  bool got_err(Glib::IOCondition c, Glib::RefPtr<Glib::IOChannel> chan);

  string tempstr;

  sigc::signal<void> signal_done;
  int done;

public:
  string output_std, output_err;

  monotone();
  ~monotone();
  void set_dir(string const & s){dir = (s.empty()?".":s); stop();}
  void set_db(string const & s){db = s; stop();}
  string get_dir(){return dir;}
  string get_db(){return db;}

  // run the Gtk event loop while waiting for the current command to finish
  void waitfor();
  // call this when the current command finished
  void when_done(sigc::slot<void> cb);
  bool is_busy();

  // run a command with 'automate stdio'
  void command(string const & cmd, vector<string> const & args);
  // run a command from the command line
  void runcmd(string const & cmd, vector<string> const & args);


  void inventory(std::vector<inventory_item> & out);
  void certs(std::string const & rev, vector<cert> & out);
  void select(std::string const & sel, vector<string> & out);
  void make_cert(std::string const & rev,
                 std::string const & name,
                 std::string const & value);
  void commit(vector<string> args, string & out);
  void diff(string const & filename, string & out);
  void diff(string const & filename, string const & rev1,
            string const & rev2, string & out);
  void cat(string const & filename, string const & rev, string & out);
  void get_revision(string const & rev, string & out);
  void get_manifest(string const & rev, string & out);
  void add(string const & file);
  void drop(string const & file);
  void revert(string const & file);
  void rename(string const & oldname, string const & newname);
  void update(vector<string> & opts, string & out);
  void update(string const & rev, string & out);
  void sync();
};

#endif
