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

namespace cvs_sync {
struct cvs_revision
{ std::vector<int> parts;

  cvs_revision(const std::string &x);
  void operator++(int);
  std::string get_string() const;
  bool is_branch() const;
  bool is_parent_of(const cvs_revision &child) const;
};

struct cvs_file_state
{ std::string revision;
  time_t last_changed;
#if 0
  bool dead;
  std::string log_message;
  cvs_file_state() : last_changed(), dead() {}
  cvs_file_state(const std::string &r, time_t lc, bool d, const std::string &lm) 
    : revision(r), last_changed(lc), dead(d), log_message(lm) {}
#endif
};

struct cvs_changeset // == cvs_key ?? rcs_delta+rcs_deltatext
{ typedef std::map<std::string,cvs_file_state> tree_state_t;

//  cvs_client::stringset_t tags; ???
  tree_state_t tree_state; // dead files do not occur here
};

struct file_state
{ time_t since_when;
  std::string cvs_version;
  std::string rcs_patch;
  std::string contents;
  std::string sha1sum;
  bool dead;
  std::string log_msg;

  file_state() : since_when(), dead() {}  
  file_state(time_t sw,const std::string &rev,bool d=false) 
  : since_when(sw), cvs_version(rev), dead(d) {}  
  bool operator==(const file_state &b) const
  { return since_when==b.since_when; }
  bool operator<(const file_state &b) const
  { return since_when<b.since_when; }
};

struct file
{ std::set<file_state> known_states;
};

struct cvs_edge // careful this name is also used in cvs_import
{ // std::string branch;
  std::string changelog;
  bool changelog_valid;
  std::string author;
  time_t time; //  std::string time;
  cvs_changeset::tree_state_t files; // this should be a state change!
//  std::string manifest; // monotone manifest
  std::string revision; // monotone revision

  cvs_edge() : changelog_valid(), time() {} 
  cvs_edge(time_t when) : changelog_valid(), time(when) {} 
  cvs_edge(const std::string &log, time_t when, const std::string &auth) 
    : changelog(log), changelog_valid(true), author(auth), time(when) {} 
  
  inline bool similar_enough(cvs_edge const & other) const
  {
    if (changelog != other.changelog)
      return false;
    if (author != other.author)
      return false;
    if (labs(time - other.time) > constants::cvs_window)
      return false;
    return true;
  }

  inline bool operator==(cvs_edge const & other) const
  {
    return // branch == other.branch &&
      changelog == other.changelog &&
      author == other.author &&
      time == other.time;
  }

  inline bool operator<(cvs_edge const & other) const
  {
    return time < other.time ||

      (time == other.time 
       && author < other.author) ||

      (time == other.time 
       && author == other.author 
       && changelog < other.changelog);
  }
};

class cvs_repository : public cvs_client
{ 
public:
  typedef cvs_changeset::tree_state_t tree_state_t;
  struct prime_log_cb;
  struct now_log_cb;
  struct now_list_cb;

private:
//  std::list<tree_state_t> tree_states;
  // zusammen mit changelog, date, author(?)
//  std::map<tree_state_t*,tree_state_t*> successor;
  std::set<cvs_edge> edges;
  std::map<std::string,file> files;
  // tag,file,rev
  std::map<std::string,std::map<std::string,std::string> > tags;
  
  void prime();
public:  
  cvs_repository(const std::string &host, const std::string &root,
             const std::string &user=std::string(), 
             const std::string &module=std::string(), 
             bool pserver=false)
      : cvs_client(host,root,user,module,pserver) {}

  std::list<std::string> get_modules();
  void set_branch(const std::string &tag);
  void ticker() const;
  const tree_state_t &now();
  const tree_state_t &find(const std::string &date,const std::string &changelog);
  const tree_state_t &next(const tree_state_t &m) const;
  
  void debug() const;
};
} // end namespace cvs_sync
