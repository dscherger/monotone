// copyright (C) 2005-2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <list>
#include <iostream>
#include <stdexcept>
#include "sanity.hh"
#include "cvs_client.hh"
#include "mtn_automate.hh"
//#include "constants.hh"
//#include "app_state.hh"
//#include "packet.hh"
class mtncvs_state;

namespace cvs_sync {
struct cvs_revision_nr
{ std::vector<int> parts;

  cvs_revision_nr(const std::string &x);
  cvs_revision_nr() {}
  void operator++();
  std::string get_string() const;
  bool is_branch() const;
  bool is_parent_of(const cvs_revision_nr &child) const;
  // get the root of the side branch (1.1.1.1.0.6 => 1.1.1.1)
  cvs_revision_nr get_branch_root() const;
  bool operator==(const cvs_revision_nr &b) const;
  bool operator<(const cvs_revision_nr &b) const;
};

// FIXME: future conversion to file_path
typedef std::string cvs_file_path;

struct file_state
{ time_t since_when; // boost::ptime
  std::string cvs_version; // cvs_revision_nr ?
  unsigned size;
  unsigned patchsize;
  bool dead;
  std::string md5sum; // hexenc<something?>
  std::string cvssha1sum; // since this can contain parts of an sha1sum this cannot be a file_id
  file_id sha1sum;
  std::string log_msg;
  std::string author;
  std::string keyword_substitution;

  file_state() : since_when(), size(), patchsize(), dead() {}  
  file_state(time_t sw,const std::string &rev,bool d=false) 
  : since_when(sw), cvs_version(rev), size(), patchsize(), dead(d) {}  
  bool operator==(const file_state &b) const
  { return since_when==b.since_when && cvs_version==cvs_version; }
  bool operator<(const file_state &b) const;
};

struct file_history
{ std::set<file_state> known_states;
};

typedef std::set<file_state>::const_iterator cvs_file_state;

// state of the files at a specific point in history, dead files do not occur here
typedef std::map<cvs_file_path,cvs_file_state> cvs_manifest;

struct cvs_edge // careful this name is also used in cvs_import
{
  std::string changelog;
  bool changelog_valid;
  std::string author;
  time_t time; // boost::ptime
  mutable time_t time2;
  mutable revision_id delta_base;
  // delta encoded if !delta_base().empty()
  mutable cvs_manifest xfiles; // manifest (or use cvs_manifest)
//  mutable unsigned cm_delta_depth; // we store a full manifest every N revisions
//  static const unsigned cm_max_delta_depth=50;
  mutable revision_id revision; // monotone revision

  // I do not want this to be 3 hours (how comes?)
  static size_t const cvs_window = 5;

  cvs_edge() : changelog_valid(), time(), time2() {} 
  cvs_edge(time_t when) : changelog_valid(), time(when), time2(when) {} 
  cvs_edge(const std::string &log, time_t when, const std::string &auth) 
    : changelog(log), changelog_valid(true), author(auth), time(when), time2(when)
  {} 
  cvs_edge(const revision_id &rid,mtncvs_state &app);
  
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

bool operator<(const file_state &,const cvs_edge &);
bool operator<=(const file_state &,const cvs_edge &);

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
  std::map<revision_id,std::set<cvs_edge>::iterator> revision_lookup;
  std::map<cvs_file_path,file_history> files;
  // tag,file,rev
  std::map<std::string,std::map<cvs_file_path,cvs_revision_nr> > tags;
  // the root of this branch (only applicable to side branches)
  std::map<cvs_file_path,cvs_revision_nr> branch_point;

  mtncvs_state &app;
  std::auto_ptr<ticker> file_id_ticker;
  std::auto_ptr<ticker> revision_ticker;
  std::auto_ptr<ticker> cvs_edges_ticker;

  // for delta encoding of files
  std::set<file_state> remove_set; // remove_state lives here
  cvs_file_state remove_state;
  
  time_t sync_since;

public:
  mtn_automate::sync_map_t create_cvs_cert_header() const;
  static void parse_cvs_cert_header(
      mtn_automate::sync_map_t const& c, std::string const& domain,
      std::string &repository, std::string& module, std::string& branch);
      
private:
  void check_split(const cvs_file_state &s, const cvs_file_state &end, 
          const std::set<cvs_edge>::iterator &e);
  void get_all_files();
  void update(std::set<file_state>::const_iterator s,
              std::set<file_state>::iterator s2,cvs_file_path const& file,
              std::string &contents);
  void store_checkout(std::set<file_state>::iterator s2,
        const cvs_client::checkout &file, std::string &file_contents);
  void store_checkout(std::set<file_state>::iterator s2,
        const cvs_client::update &file, std::string &file_contents);
  void store_update(std::set<file_state>::const_iterator s,
        std::set<file_state>::iterator s2,const cvs_client::update &u,
        std::string &file_contents);
  void fill_manifests(std::set<cvs_edge>::iterator e);
  void commit_cvs2mtn(std::set<cvs_edge>::iterator e);

  void store_contents(file_data const&contents, file_id &sha1sum);
  void store_delta(file_data const& new_contents, file_data const &old_contents, 
		file_id const&from, file_id &to);
  
  void cert_cvs(const cvs_edge &e);
  cvs_file_state remember(std::set<file_state> &s,const file_state &fs, cvs_file_path const& filename);
  void join_edge_parts(std::set<cvs_edge>::iterator i);
  std::set<cvs_edge>::iterator last_known_revision();
  std::set<cvs_edge>::iterator commit_mtn2cvs(
      std::set<cvs_edge>::iterator parent, const revision_id &rid, bool &fail);
  const cvs_manifest &get_files(const cvs_edge &e);
  // try harder (reconnect if something goes wrong)
  struct checkout CheckOut2(const cvs_file_path &file, const std::string &revision);
  void takeover_dir(const std::string &path);
  
  void store_modules();
  void retrieve_modules();
  std::string gather_merge_information(revision_id const& id);
  void attach_sync_state(cvs_edge & e,mtn_automate::manifest_map const& oldmanifest,
        mtn_automate::cset &cs);
  mtn_automate::sync_map_t create_sync_state(cvs_edge const& e);
  
public: // semi public interface for push/pull
  void prime();
  void update();
  void commit();
  void process_sync_info(mtn_automate::sync_map_t const& sync_info, revision_id const& rid);
  bool empty() const { return edges.empty() && files.empty(); }
  void parse_module_paths(mtn_automate::sync_map_t const&);

  const cvs_manifest &get_files(const revision_id &e);
  
  static time_t posix2time_t(std::string s);
  static std::string time_t2human(const time_t &t);

  void takeover();
  std::string debug_file(std::string const& name);
  
public:  
  cvs_repository(mtncvs_state &app, const std::string &repository, 
      const std::string &module, const std::string &branch=std::string(),
      bool connect=true);

  std::string debug() const;
  
#if 0 // yet unimplemented and unneeded ~nice~ API ideas
  std::list<std::string> get_modules();
  void set_branch(const std::string &tag);
  const tree_state_t &find(const std::string &date,const std::string &changelog);
  const tree_state_t &next(const tree_state_t &m) const;
#endif  
};

void pull(const std::string &repository, const std::string &module,
            std::string const& branch, mtncvs_state &app);
void push(const std::string &repository, const std::string &module,
            std::string const& branch, mtncvs_state &app);
void debug(const std::string &command, const std::string &arg, mtncvs_state &app);
void takeover(mtncvs_state &app, const std::string &module);
void test(mtncvs_state &app);
} // end namespace cvs_sync
