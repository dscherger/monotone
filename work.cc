// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <sstream>
#include <cstdio>
#include <cstring>
#include <cerrno>

#include "app_state.hh"
#include "basic_io.hh"
#include "change_set.hh"
#include "file_io.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "vocab.hh"
#include "work.hh"

// working copy / book-keeping file code

using namespace std;

// attribute map file

string const attr_file_name(".mt-attrs");

void 
file_itemizer::visit_file(file_path const & path)
{
  if (app.restriction_includes(path) && known.find(path) == known.end())
    {
      if (app.lua.hook_ignore_file(path))
        ignored.insert(path);
      else
        unknown.insert(path);
    }
}

class 
addition_builder 
  : public tree_walker
{
  app_state & app;
  change_set::path_rearrangement & pr;
  path_set ps;
  attr_map & am_attrs;
public:
  addition_builder(app_state & a, 
                   change_set::path_rearrangement & pr,
                   path_set & p,
                   attr_map & am)
    : app(a), pr(pr), ps(p), am_attrs(am)
  {}
  virtual void visit_file(file_path const & path);
};

void 
addition_builder::visit_file(file_path const & path)
{     
  if (app.lua.hook_ignore_file(path))
    {
      P(F("skipping ignorable file %s\n") % path);
      return;
    }  

  if (ps.find(path) != ps.end())
    {
      P(F("skipping %s, already accounted for in working copy\n") % path);
      return;
    }

  P(F("adding %s to working copy add set\n") % path);
  ps.insert(path);
  pr.added_files.insert(path);

  map<string, string> attrs;
  app.lua.hook_init_attributes(path, attrs);
  if (attrs.size() > 0)
    am_attrs[path] = attrs;
}

void
build_additions(vector<file_path> const & paths, 
                manifest_map const & man,
                app_state & app,
                change_set::path_rearrangement & pr)
{
  change_set::path_rearrangement pr_new, pr_concatenated;
  change_set cs_new;

  path_set ps;
  attr_map am_attrs;
  extract_path_set(man, ps);
  apply_path_rearrangement(pr, ps);    

  addition_builder build(app, pr_new, ps, am_attrs);

  for (vector<file_path>::const_iterator i = paths.begin(); i != paths.end(); ++i)
    // NB.: walk_tree will handle error checking for non-existent paths
    walk_tree(*i, build);

  if (am_attrs.size () > 0)
    {
      // add .mt-attrs to manifest if not already registered
      file_path attr_path;
      get_attr_path(attr_path);

      if ((man.find(attr_path) == man.end ()) && 
          (pr_new.added_files.find(attr_path) == pr_new.added_files.end()))
        {
          P(F("registering %s file in working copy\n") % attr_path);
          pr.added_files.insert(attr_path);
        }

      // read attribute map if available
      data attr_data;
      attr_map attrs;

      if (path_exists(attr_path))
        {
          read_data(attr_path, attr_data);
          read_attr_map(attr_data, attrs);
        }

      // add new attribute entries
      for (attr_map::const_iterator i = am_attrs.begin();
           i != am_attrs.end(); ++i)
        {
          map<string, string> m = i->second;
          
          for (map<string, string>::const_iterator j = m.begin();
               j != m.end(); ++j)
            {
              P(F("adding attribute '%s' on file %s to %s\n") % j->first % i->first % attr_file_name);
              attrs[i->first][j->first] = j->second;
            }
        }

      // write out updated map
      write_attr_map(attr_data, attrs);
      write_data(attr_path, attr_data);
    }

  normalize_path_rearrangement(pr_new);
  concatenate_rearrangements(pr, pr_new, pr_concatenated);
  pr = pr_concatenated;
}

static bool
known_path(file_path const & p,
           path_set const & ps,
           bool & path_is_directory)
{
  std::string path_as_dir = p.as_internal() + "/";
  for (path_set::const_iterator i = ps.begin(); i != ps.end(); ++i)
    {
      if (*i == p) 
        {
          path_is_directory = false;
          return true;
        }
      else if (i->as_internal().find(path_as_dir) == 0)
        {
          path_is_directory = true;
          return true;
        }
    }
  return false;
}

void
build_deletions(vector<file_path> const & paths, 
                manifest_map const & man,
                app_state & app,
                change_set::path_rearrangement & pr)
{
  change_set::path_rearrangement pr_new, pr_concatenated;
  path_set ps;
  extract_path_set(man, ps);
  apply_path_rearrangement(pr, ps);    

  // read attribute map if available
  file_path attr_path;
  get_attr_path(attr_path);

  data attr_data;
  attr_map attrs;

  if (path_exists(attr_path))
  {
    read_data(attr_path, attr_data);
    read_attr_map(attr_data, attrs);
  }

  bool updated_attr_map = false;

  for (vector<file_path>::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      bool dir_p = false;
  
      N(!i->empty(), F("invalid path ''"));

      if (! known_path(*i, ps, dir_p))
        {
          P(F("skipping %s, not currently tracked\n") % *i);
          continue;
        }

      P(F("adding %s to working copy delete set\n") % *i);

      if (dir_p) 
        {
          E(false, F("sorry -- 'drop <directory>' is currently broken.\n"
                     "try 'find %s -type f | monotone drop -@-'\n") % (*i));
          pr_new.deleted_dirs.insert(*i);
        }
      else 
        {
          pr_new.deleted_files.insert(*i);

          // delete any associated attributes for this file
          if (1 == attrs.erase(*i))
            {
              updated_attr_map = true;
              P(F("dropped attributes for file %s from %s\n") % (*i) % attr_file_name);
            }
          if (app.execute && path_exists(*i))
            delete_file(*i);
        }
  }

  normalize_path_rearrangement(pr_new);
  concatenate_rearrangements(pr, pr_new, pr_concatenated);
  pr = pr_concatenated;

  // write out updated map if necessary
  if (updated_attr_map)
    {
      write_attr_map(attr_data, attrs);
      write_data(attr_path, attr_data);
    }
}

void 
build_rename(file_path const & src,
             file_path const & dst,
             manifest_map const & man,
             app_state & app,
             change_set::path_rearrangement & pr)
{
  N(!src.empty(), F("invalid source path ''"));
  N(!dst.empty(), F("invalid destination path ''"));

  change_set::path_rearrangement pr_new, pr_concatenated;
  path_set ps;
  extract_path_set(man, ps);
  apply_path_rearrangement(pr, ps);    

  bool src_dir_p = false;
  bool dst_dir_p = false;

  N(known_path(src, ps, src_dir_p), 
    F("%s does not exist in current revision\n") % src);

  N(!known_path(dst, ps, dst_dir_p), 
    F("%s already exists in current revision\n") % dst);

  P(F("adding %s -> %s to working copy rename set\n") % src % dst);
  if (src_dir_p)
    pr_new.renamed_dirs.insert(std::make_pair(src, dst));
  else 
    pr_new.renamed_files.insert(std::make_pair(src, dst));

  if (app.execute && (path_exists(src) || !path_exists(dst)))
    move_path(src, dst);

  // read attribute map if available
  file_path attr_path;
  get_attr_path(attr_path);

  if (path_exists(attr_path))
  {
    data attr_data;
    read_data(attr_path, attr_data);
    attr_map attrs;
    read_attr_map(attr_data, attrs);

    // make sure there aren't pre-existing attributes that we'd accidentally
    // pick up
    N(attrs.find(dst) == attrs.end(), 
      F("%s has existing attributes in %s; clean them up first") % dst % attr_file_name);

    // only write out a new attribute map if we find attrs to move
    attr_map::iterator a = attrs.find(src);
    if (a != attrs.end())
    {
      attrs[dst] = (*a).second;
      attrs.erase(a);

      P(F("moving attributes for %s to %s\n") % src % dst);

      write_attr_map(attr_data, attrs);
      write_data(attr_path, attr_data);
    }
  }

  normalize_path_rearrangement(pr_new);
  concatenate_rearrangements(pr, pr_new, pr_concatenated);
  pr = pr_concatenated;
}

// work file containing rearrangement from uncommitted adds/drops/renames

std::string const work_file_name("work");

static void get_work_path(bookkeeping_path & w_path)
{
  w_path = bookkeeping_root / work_file_name;
  L(F("work path is %s\n") % w_path);
}

void get_path_rearrangement(change_set::path_rearrangement & w)
{
  bookkeeping_path w_path;
  get_work_path(w_path);
  if (path_exists(w_path))
    {
      L(F("checking for un-committed work file %s\n") % w_path);
      data w_data;
      read_data(w_path, w_data);
      read_path_rearrangement(w_data, w);
      L(F("read rearrangement from %s\n") % w_path);
    }
  else
    {
      L(F("no un-committed work file %s\n") % w_path);
    }
}

void remove_path_rearrangement()
{
  bookkeeping_path w_path;
  get_work_path(w_path);
  if (file_exists(w_path))
    delete_file(w_path);
}

void put_path_rearrangement(change_set::path_rearrangement & w)
{
  bookkeeping_path w_path;
  get_work_path(w_path);
  
  if (w.empty())
    {
      if (file_exists(w_path))
        delete_file(w_path);
    }
  else
    {
      data w_data;
      write_path_rearrangement(w, w_data);
      write_data(w_path, w_data);
    }
}

// revision file name 

std::string revision_file_name("revision");

static void get_revision_path(bookkeeping_path & m_path)
{
  m_path = bookkeeping_root / revision_file_name;
  L(F("revision path is %s\n") % m_path);
}

void get_revision_id(revision_id & c)
{
  c = revision_id();
  bookkeeping_path c_path;
  get_revision_path(c_path);

  require_path_is_file(c_path,
                       F("working copy is corrupt: %s does not exist") % c_path,
                       F("working copy is corrupt: %s is a directory") % c_path);

  data c_data;
  L(F("loading revision id from %s\n") % c_path);
  try
    {
      read_data(c_path, c_data);
    }
  catch(std::exception & e)
    {
      N(false, F("Problem with working directory: %s is unreadable") % c_path);
    }
  c = revision_id(remove_ws(c_data()));
}

void put_revision_id(revision_id const & rev)
{
  bookkeeping_path c_path;
  get_revision_path(c_path);
  L(F("writing revision id to %s\n") % c_path);
  data c_data(rev.inner()() + "\n");
  write_data(c_path, c_data);
}

void
get_base_revision(app_state & app, 
                  revision_id & rid,
                  manifest_id & mid,
                  manifest_map & man)
{
  man.clear();

  get_revision_id(rid);

  if (!null_id(rid))
    {

      N(app.db.revision_exists(rid),
        F("base revision %s does not exist in database\n") % rid);
      
      app.db.get_revision_manifest(rid, mid);
      L(F("old manifest is %s\n") % mid);
      
      N(app.db.manifest_version_exists(mid),
        F("base manifest %s does not exist in database\n") % mid);
      
      app.db.get_manifest(mid, man);
    }

  L(F("old manifest has %d entries\n") % man.size());
}

void
get_base_manifest(app_state & app, 
                  manifest_map & man)
{
  revision_id rid;
  manifest_id mid;
  get_base_revision(app, rid, mid, man);
}


// user log file

string const user_log_file_name("log");

void
get_user_log_path(bookkeeping_path & ul_path)
{
  ul_path = bookkeeping_root / user_log_file_name;
  L(F("user log path is %s\n") % ul_path);
}

void
read_user_log(data & dat)
{
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);

  if (file_exists(ul_path))
    {
      read_data(ul_path, dat);
    }
}

void
write_user_log(data const & dat)
{
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);

  write_data(ul_path, dat);
}

void
blank_user_log()
{
  data empty;
  bookkeeping_path ul_path;
  get_user_log_path(ul_path);
  write_data(ul_path, empty);
}

bool
has_contents_user_log()
{
  data user_log_message;
  read_user_log(user_log_message);
  return user_log_message().length() > 0;
}

// options map file

string const options_file_name("options");

void 
get_options_path(bookkeeping_path & o_path)
{
  o_path = bookkeeping_root / options_file_name;
  L(F("options path is %s\n") % o_path);
}

void 
read_options_map(data const & dat, options_map & options)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss, "MT/options");
  basic_io::tokenizer tok(src);
  basic_io::parser parser(tok);

  // don't clear the options which will have settings from the command line
  // options.clear(); 

  std::string opt, val;
  while (parser.symp())
    {
      parser.sym(opt);
      parser.str(val);
      // options[opt] = val;      
      // use non-replacing insert verses replacing with options[opt] = val;
      options.insert(make_pair(opt, val)); 
    }
}

void 
write_options_map(data & dat, options_map const & options)
{
  std::ostringstream oss;
  basic_io::printer pr(oss);

  basic_io::stanza st;
  for (options_map::const_iterator i = options.begin();
       i != options.end(); ++i)
    st.push_str_pair(i->first, i->second());

  pr.print_stanza(st);
  dat = oss.str();
}

// local dump file

static string const local_dump_file_name("debug");

void get_local_dump_path(bookkeeping_path & d_path)
{
  d_path = bookkeeping_root / local_dump_file_name;
  L(F("local dump path is %s\n") % d_path);
}

// inodeprint file

static string const inodeprints_file_name("inodeprints");

void
get_inodeprints_path(bookkeeping_path & ip_path)
{
  ip_path = bookkeeping_root / inodeprints_file_name;
}

bool
in_inodeprints_mode()
{
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  return file_exists(ip_path);
}

void
read_inodeprints(data & dat)
{
  I(in_inodeprints_mode());
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  read_data(ip_path, dat);
}

void
write_inodeprints(data const & dat)
{
  I(in_inodeprints_mode());
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  write_data(ip_path, dat);
}

void
enable_inodeprints()
{
  bookkeeping_path ip_path;
  get_inodeprints_path(ip_path);
  data dat;
  write_data(ip_path, dat);
}

void 
get_attr_path(file_path & a_path)
{
  a_path = file_path_internal(attr_file_name);
  L(F("attribute map path is %s\n") % a_path);
}

namespace
{
  namespace syms
  {
    std::string const file("file");
  }
}

void 
read_attr_map(data const & dat, attr_map & attr)
{
  std::istringstream iss(dat());
  basic_io::input_source src(iss, attr_file_name);
  basic_io::tokenizer tok(src);
  basic_io::parser parser(tok);

  std::string file, name, value;

  attr.clear();

  while (parser.symp(syms::file))
    {
      parser.sym();
      parser.str(file);
      file_path fp = file_path_internal(file);

      while (parser.symp() && 
             !parser.symp(syms::file)) 
        {
          parser.sym(name);
          parser.str(value);
          attr[fp][name] = value;
        }
    }
}

void 
write_attr_map(data & dat, attr_map const & attr)
{
  std::ostringstream oss;
  basic_io::printer pr(oss);
  
  for (attr_map::const_iterator i = attr.begin();
       i != attr.end(); ++i)
    {
      basic_io::stanza st;
      st.push_str_pair(syms::file, i->first.as_internal());

      for (std::map<std::string, std::string>::const_iterator j = i->second.begin();
           j != i->second.end(); ++j)
          st.push_str_pair(j->first, j->second);          

      pr.print_stanza(st);
    }

  dat = oss.str();
}


static void 
apply_attributes(app_state & app, attr_map const & attr)
{
  for (attr_map::const_iterator i = attr.begin();
       i != attr.end(); ++i)
      for (std::map<std::string, std::string>::const_iterator j = i->second.begin();
           j != i->second.end(); ++j)
        app.lua.hook_apply_attribute (j->first,
                                      i->first, 
                                      j->second);
}

string const encoding_attribute("encoding");
string const binary_encoding("binary");
string const default_encoding("default");

string const manual_merge_attribute("manual_merge");

static bool find_in_attr_map(attr_map const & attr,
                             file_path const & file,
                             std::string const & attr_key,
                             std::string & attr_val)
{
  attr_map::const_iterator f = attr.find(file);
  if (f == attr.end())
    return false;

  std::map<std::string, std::string>::const_iterator a = f->second.find(attr_key);
  if (a == f->second.end())
    return false;

  attr_val = a->second;
  return true;
}

void 
read_attr_map_from_db(manifest_map const & man,
		      attr_map & attr,
                      app_state & app)
{
  file_path fp;
  get_attr_path(fp);
  manifest_map::const_iterator i = man.find(fp);
  if (i == man.end())
    return;

  file_id fid = manifest_entry_id(i);
  if (!app.db.file_version_exists(fid))
    return;

  file_data attr_data;
  app.db.get_file_version(fid, attr_data);

  read_attr_map(data(attr_data.inner()()), attr);
}

bool get_attribute_from_db(file_path const & file,
                           std::string const & attr_key,
                           manifest_map const & man,
                           std::string & attr_val,
                           app_state & app)
{
  attr_map attr;
  read_attr_map_from_db(man, attr, app);
  return find_in_attr_map(attr, file, attr_key, attr_val);
}

bool get_attribute_from_working_copy(file_path const & file,
                                     std::string const & attr_key,
                                     std::string & attr_val)
{
  file_path fp;
  get_attr_path(fp);
  if (!file_exists(fp))
    return false;
  
  data attr_data;
  read_data(fp, attr_data);

  attr_map attr;
  read_attr_map(attr_data, attr);

  return find_in_attr_map(attr, file, attr_key, attr_val);
}

void update_any_attrs(app_state & app)
{
  file_path fp;
  data attr_data;
  attr_map attr;

  get_attr_path(fp);
  if (!file_exists(fp))
    return;

  read_data(fp, attr_data);
  read_attr_map(attr_data, attr);
  apply_attributes(app, attr);
}
