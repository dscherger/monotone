// copyright (C) 2005 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <list>
#include <iostream>
#include <stdexcept>
#include "sanity.hh"
#include "cvs_client.hh"
#include "constants.hh"
#include "app_state.hh"
#include "packet.hh"

namespace cvs_sync {
struct cvs_revision_nr
{ std::vector<int> parts;

  cvs_revision_nr(const std::string &x);
  void operator++(int);
  std::string get_string() const;
  bool is_branch() const;
  bool is_parent_of(const cvs_revision_nr &child) const;
};

struct file_state
{ time_t since_when;
  std::string cvs_version; // cvs_revision_nr ?
  unsigned size;
  unsigned patchsize;
  bool dead;
  std::string md5sum;
  hexenc<id> sha1sum;
  std::string log_msg;
  std::string keyword_substitution;

  file_state() : since_when(), size(), patchsize(), dead() {}  
  file_state(time_t sw,const std::string &rev,bool d=false) 
  : since_when(sw), cvs_version(rev), size(), patchsize(), dead(d) {}  
  bool operator==(const file_state &b) const
  { return since_when==b.since_when; }
  bool operator<(const file_state &b) const
  { return since_when<b.since_when; }
};

struct file_history
{ std::set<file_state> known_states;
};

typedef std::set<file_state>::const_iterator cvs_file_state;

// state of the files at a specific point in history, dead files do not occur here
typedef std::map<std::string,cvs_file_state> cvs_manifest;

struct cvs_edge // careful this name is also used in cvs_import
{
  std::string changelog;
  bool changelog_valid;
  std::string author;
  time_t time;
  mutable time_t time2;
  mutable cvs_manifest files; // manifest (or use cvs_manifest)
  mutable hexenc<id> revision; // monotone revision

  // I do not want this to be 3 hours (how comes?)
  static size_t const cvs_window = 5;

  cvs_edge() : changelog_valid(), time(), time2() {} 
  cvs_edge(time_t when) : changelog_valid(), time(when), time2(when) {} 
  cvs_edge(const std::string &log, time_t when, const std::string &auth) 
    : changelog(log), changelog_valid(true), author(auth), time(when), time2(when)
  {} 
  
  bool similar_enough(cvs_edge const & other) const;
  inline bool operator==(cvs_edge const & other) const
  {
    return // branch == other.branch &&
      changelog == other.changelog &&
      author == other.author &&
      time == other.time;
  }
  bool operator<(cvs_edge const & other) const;
};

class cvs_repository : public cvs_client
{ 
public:
  typedef cvs_manifest tree_state_t;
  struct prime_log_cb;
  struct get_all_files_log_cb;
  struct get_all_files_list_cb;
  struct update_cb;

private:
  std::set<cvs_edge> edges;
  std::map<std::string,file_history> files;
  // tag,file,rev
  std::map<std::string,std::map<std::string,std::string> > tags;

  app_state &app;
  std::auto_ptr<ticker> file_id_ticker;
  std::auto_ptr<ticker> revision_ticker;
  std::auto_ptr<ticker> cvs_edges_ticker;

  void check_split(const cvs_file_state &s, const cvs_file_state &end, 
          const std::set<cvs_edge>::iterator &e);
  void get_all_files();
  void update(std::set<file_state>::const_iterator s,
              std::set<file_state>::iterator s2,const std::string &file,
              std::string &contents);
  void store_checkout(std::set<file_state>::iterator s2,
        const cvs_client::checkout &file, std::string &file_contents);
  void store_update(std::set<file_state>::const_iterator s,
        std::set<file_state>::iterator s2,const cvs_client::update &u,
        std::string &file_contents);
public:  
  void prime();
  void update();
//  void compact_files();
  cvs_file_state remember(std::set<file_state> &s,const file_state &fs);
  void join_edge_parts(std::set<cvs_edge>::iterator i);
  
public:  
  cvs_repository(app_state &app, const std::string &repository, const std::string &module);

  std::list<std::string> get_modules();
  void set_branch(const std::string &tag);
  const tree_state_t &find(const std::string &date,const std::string &changelog);
  const tree_state_t &next(const tree_state_t &m) const;
  
  void debug() const;
  void store_contents(const std::string &contents, hexenc<id> &sha1sum);
//  void apply_delta(std::string &contents, const std::string &patch);
  void store_delta(const std::string &new_contents, const std::string &old_contents, const std::string &patch, const hexenc<id> &from, hexenc<id> &to);
  
  void cert_cvs(const cvs_edge &e, packet_consumer & pc);
  
  bool empty() const { return edges.empty() && files.empty(); }
  void process_certs(const std::vector< revision<cert> > &certs);
};

void sync(const std::string &repository, const std::string &module,
            app_state &app);
} // end namespace cvs_sync
