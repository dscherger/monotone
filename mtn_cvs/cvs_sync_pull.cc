// copyright (C) 2005-2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "cvs_sync.hh"
#include "mtncvs_state.hh"
#include "keys.hh"
#include "transforms.hh"
#include <vector>
#include <boost/lexical_cast.hpp>
#include "botan/md5.h"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/greg_date.hpp>
#include <fstream>
#include <sys/stat.h>
#include "stringtok.hh"
#include "piece_table.hh"
#include "safe_map.hh"
#include <boost/format.hpp>

#ifdef WIN32
#define sleep(x) _sleep(x)
#endif

using namespace std;
using namespace cvs_sync;

void cvs_repository::store_contents(file_data const &dat, file_id &sha1sum)
{
  if (file_id_ticker.get()) ++(*file_id_ticker);
  sha1sum=app.put_file(dat);
}

static void apply_delta(piece::piece_table &contents, const std::string &patch)
{ piece::piece_table after;
  piece::apply_diff(contents,after,patch);
  std::swap(contents,after);
}

void cvs_repository::store_delta(file_data const& new_contents, 
          file_data const& old_contents, 
          file_id const&from, file_id &to)
{ if (old_contents.inner()().empty())
  { store_contents(new_contents, to);
    return;
  }
  if (file_id_ticker.get()) ++(*file_id_ticker);
  to=app.put_file(new_contents,from);
}

void cvs_repository::store_update(std::set<file_state>::const_iterator s,
        std::set<file_state>::iterator s2,const cvs_client::update &u,
        std::string &contents)
{
  if (u.removed)
  { const_cast<bool&>(s2->dead)=true;
  }
  else if (!u.checksum.empty())
  { // const_cast<std::string&>(s2->rcs_patch)=u.patch;
    const_cast<std::string&>(s2->md5sum)=u.checksum;
    const_cast<unsigned&>(s2->patchsize)=u.patch.size();
    const_cast<std::string&>(s2->keyword_substitution)=u.keyword_substitution;
    // I(s2->since_when==u.mod_time);
    if (u.mod_time!=s2->since_when && u.mod_time!=-1)
    { W(F("update time %s and log time %s disagree\n") % time_t2human(u.mod_time) % time_t2human(s2->since_when));
    }
    std::string old_contents=contents;
    { piece::piece_table file_contents;
      piece::index_deltatext(contents,file_contents);
      apply_delta(file_contents, u.patch);
      piece::build_string(file_contents, contents);
      piece::reset();
    }
    // check md5
    Botan::MD5 hash;
    std::string md5sum=xform<Botan::Hex_Decoder>(u.checksum,origin::internal);
    I(md5sum.size()==hash.OUTPUT_LENGTH);
    Botan::SecureVector<Botan::byte> hashval=hash.process(contents);
    I(hashval.size()==hash.OUTPUT_LENGTH);
    unsigned hashidx=hash.OUTPUT_LENGTH;
    for (;hashidx && hashval[hashidx-1]==Botan::byte(md5sum[hashidx-1]);--hashidx) ;
    if (!hashidx)
    { store_delta(file_data(contents,origin::internal), file_data(old_contents,origin::internal), s->sha1sum, 
			const_cast<file_id&>(s2->sha1sum));
    }
    else
    { E(false, origin::network, F("MD5 sum %s<>%s") % u.checksum 
          % xform<Botan::Hex_Encoder>(std::string(hashval.begin(),hashval.end()),origin::internal));
    }
  }
  else
  { if (!s->sha1sum.inner()().empty()) 
    // we default to patch if it's at all possible
      store_delta(file_data(u.contents,origin::internal), file_data(contents,origin::internal), s->sha1sum, const_cast<file_id&>(s2->sha1sum));
    else
      store_contents(file_data(u.contents,origin::internal), const_cast<file_id&>(s2->sha1sum));
    const_cast<unsigned&>(s2->size)=u.contents.size();
    contents=u.contents;
    const_cast<std::string&>(s2->keyword_substitution)=u.keyword_substitution;
  }
}

// s2 gets changed
void cvs_repository::update(std::set<file_state>::const_iterator s,
        std::set<file_state>::iterator s2,const std::string &file,
        std::string &contents)
{
  cvs_revision_nr srev(s->cvs_version);
  MM(file);
  MM(s->cvs_version);
  MM(s2->cvs_version);
  if (!srev.is_parent_of(s2->cvs_version)) 
    std::cerr << "Inconsistency "<< file << ": " << s->cvs_version 
              << "->" << s2->cvs_version << "\n" << debug() << '\n';
  I(srev.is_parent_of(s2->cvs_version));
  if (s->dead)
  { 
    // this might fail (?) because we issued an Entry somewhere above
    // but ... we can specify the correct directory!
    cvs_client::update c=Update(file,s2->cvs_version);
    I(!c.removed); // dead->dead is no change, so shouldn't get a number
    I(!s2->dead);
    // I(s2->since_when==c.mod_time);
    if (c.mod_time!=s2->since_when && c.mod_time!=-1 && s2->since_when!=sync_since)
    { W(F("checkout time %s and log time %s disagree\n") % time_t2human(c.mod_time) % time_t2human(s2->since_when));
    }
    store_contents(file_data(c.contents,origin::internal), const_cast<file_id&>(s2->sha1sum));
    const_cast<unsigned&>(s2->size)=c.contents.size();
    contents=c.contents;
    const_cast<std::string&>(s2->keyword_substitution)=c.keyword_substitution;
  }
  else if (s2->dead) // short circuit if we already know it's dead
  { L(FL("file %s: revision %s already known to be dead\n") % file % s2->cvs_version);
  }
  else
  { cvs_client::update u=Update(file,s->cvs_version,s2->cvs_version,s->keyword_substitution);
    try 
    { store_update(s,s2,u,contents);
    } catch (recoverable_failure &e)
    { W(F("Update: patching failed with %s\n") % e.what());
      cvs_client::update c=Update(file,s2->cvs_version);
      if (c.mod_time!=s2->since_when && c.mod_time!=-1 && s2->since_when!=sync_since)
      { W(F("checkout time %s and log time %s disagree\n") % time_t2human(c.mod_time) % time_t2human(s2->since_when));
      }
      const_cast<std::string&>(s2->md5sum)="";
      const_cast<unsigned&>(s2->patchsize)=0;
      store_contents(file_data(c.contents,origin::internal), const_cast<file_id&>(s2->sha1sum));
      const_cast<unsigned&>(s2->size)=c.contents.size();
      contents=c.contents;
      const_cast<std::string&>(s2->keyword_substitution)=c.keyword_substitution;
    } catch (std::exception &e)
    { W(F("Update: patching failed with %s\n") % e.what());
      cvs_client::update c=Update(file,s2->cvs_version);
      if (c.mod_time!=s2->since_when && c.mod_time!=-1 && s2->since_when!=sync_since)
      { W(F("checkout time %s and log time %s disagree\n") % time_t2human(c.mod_time) % time_t2human(s2->since_when));
      }
      const_cast<std::string&>(s2->md5sum)="";
      const_cast<unsigned&>(s2->patchsize)=0;
      store_contents(file_data(c.contents,origin::internal), const_cast<file_id&>(s2->sha1sum));
      const_cast<unsigned&>(s2->size)=c.contents.size();
      contents=c.contents;
      const_cast<std::string&>(s2->keyword_substitution)=c.keyword_substitution;
    }
  }
}

void cvs_repository::store_checkout(std::set<file_state>::iterator s2,
        const cvs_client::checkout &c, std::string &file_contents)
{ const_cast<bool&>(s2->dead)=c.dead;
  if (!c.dead)
  { // I(c.mod_time==s2->since_when);
    if (c.mod_time!=s2->since_when && c.mod_time!=-1 && s2->since_when!=sync_since)
    { W(F("checkout time %s and log time %s disagree\n") % time_t2human(c.mod_time) % time_t2human(s2->since_when));
    }
    store_contents(file_data(c.contents,origin::internal), const_cast<file_id&>(s2->sha1sum));
    const_cast<unsigned&>(s2->size)=c.contents.size();
    file_contents=c.contents;
    const_cast<std::string&>(s2->keyword_substitution)=c.keyword_substitution;
    const_cast<int&>(s2->mode)=c.mode;
  }
}

void cvs_repository::store_checkout(std::set<file_state>::iterator s2,
        const cvs_client::update &c, std::string &file_contents)
{ const_cast<bool&>(s2->dead)=c.removed;
  if (!c.removed)
  { // I(c.mod_time==s2->since_when);
    if (c.mod_time!=s2->since_when && c.mod_time!=-1 && s2->since_when!=sync_since)
    { W(F("checkout time %s and log time %s disagree\n") % time_t2human(c.mod_time) % time_t2human(s2->since_when));
    }
    store_contents(file_data(c.contents,origin::internal), const_cast<file_id&>(s2->sha1sum));
    const_cast<unsigned&>(s2->size)=c.contents.size();
    file_contents=c.contents;
    const_cast<std::string&>(s2->keyword_substitution)=c.keyword_substitution;
    const_cast<int&>(s2->mode)=c.mode;
  }
}

void cvs_repository::attach_sync_state(cvs_edge & e,mtn_automate::manifest_map const& oldmanifest,
        mtn_automate::cset &cs)
{ mtn_automate::sync_map_t state=create_sync_state(e);
  bool any_change=false;
  // added and changed attributes
  for (mtn_automate::sync_map_t::const_iterator i=state.begin(); 
        i!=state.end(); ++i)
  { 
    mtn_automate::manifest_map::const_iterator f
          = oldmanifest.find(file_path(i->first.first));
    if (f==oldmanifest.end()) 
    { // only add attributes on existing nodes
      if (cs.dirs_added.find(i->first.first)!=cs.dirs_added.end()
          || cs.files_added.find(i->first.first)!=cs.files_added.end())
      { cs.attrs_set[i->first]=i->second;
        any_change=true;
      }
    }
    else
    {
      mtn_automate::attr_map_t::const_iterator a
          = f->second.second.find(i->first.second);
      if (a==f->second.second.end()) cs.attrs_set[i->first]=i->second;
      else if (a->second!=i->second)
      {
        cs.attrs_set[i->first]=i->second;
        any_change=true;
      }
    }
  }
  // deleted attributes
  for (mtn_automate::manifest_map::const_iterator i=oldmanifest.begin(); 
        i!=oldmanifest.end(); ++i)
  {
    file_path sp=i->first;
    for (mtn_automate::attr_map_t::const_iterator a=i->second.second.begin();
              a!=i->second.second.end(); ++a)
    {
      mtn_automate::sync_map_t::const_iterator f
          =state.find(std::make_pair(sp,a->first));
      // we do not have to delete attributes of deleted notes 
      if (f==state.end() && cs.nodes_deleted.find(sp)==cs.nodes_deleted.end())
      {
        cs.attrs_cleared.insert(std::make_pair(sp,a->first));
        any_change=true;
      }
    }
  }
  // delete old dummy attribute if present
  { mtn_automate::manifest_map::const_iterator f=oldmanifest.find(file_path_internal(""));
    if (f!=oldmanifest.end() && f->second.second.find(attr_key(app.opts.domain+":touch",origin::internal))!=f->second.second.end())
    { cs.attrs_cleared.insert(std::make_pair(file_path_internal(""),attr_key(app.opts.domain+":touch",origin::internal)));
      any_change=true;
    }
  }
  if (!any_change) // this happens if only deletions happened
  { cs.attrs_set[std::make_pair(file_path_internal(""),attr_key(app.opts.domain+":touch",origin::internal))]
        =attr_value("synchronized",origin::internal);
  }
}

static
void add_missing_parents(mtn_automate::manifest_map const& oldr, 
              file_path const & sp, mtn_automate::cset & cs)
{ 
  std::vector<std::pair<file_path,path_component> > components;
  L(FL("add_missing_parents(,%s,)\n") % sp);
  file_path sub=sp;
  do
  {
     path_component comp;
     file_path sub2=sub;
     // dirname_basename cannot output to the same object
     sub2.dirname_basename(sub,comp);
     components.push_back(std::make_pair(sub,comp)); 
  } while (!sub.empty());
  for (std::vector<std::pair<file_path,path_component> >::const_reverse_iterator i=components.rbegin();
    i!=static_cast<std::vector<std::pair<file_path,path_component> >::const_reverse_iterator>(components.rend());
    ++i)
  { L(FL("path comp '%s'\n") % i->first);
    // already added?
    if (cs.dirs_added.find(i->first)!=cs.dirs_added.end()) continue;
    mtn_automate::manifest_map::const_iterator mi=oldr.find(i->first);
    if (mi==oldr.end())
    { L(FL("adding directory %s\n") % i->first);
      safe_insert(cs.dirs_added, i->first);
    }
  }
}

// compare the new manifest with the old roster and fill the cset accordingly
static void 
build_change_set(const cvs_client &c, mtn_automate::manifest_map const& oldr, cvs_manifest &newm,
                 mtn_automate::cset &cs, cvs_file_state const& remove_state)
{
//  cvs_manifest cvs_delta;

//  node_map const & nodes = oldr.all_nodes();
  L(FL("build_change_set(%d,%d,)\n") % oldr.size() % newm.size());
  
  for (mtn_automate::manifest_map::const_iterator f = oldr.begin(); f != oldr.end(); ++f)
    { if (null_id(f->second.first)) continue; // directory
      
      cvs_manifest::const_iterator fn = newm.find(f->first.as_internal());
      if (fn==newm.end())
      {  
        L(FL("deleting file '%s'\n") % f->first);
        safe_insert(cs.nodes_deleted, f->first);
//        cvs_delta[path.as_internal()]=remove_state;
      }
      else 
        { if (f->second.first == fn->second->sha1sum)
            {
//              L(FL("skipping preserved entry state '%s' on '%s'\n")
//                % fn->second->sha1sum % fn->first);         
            }
          else
            {
              L(FL("applying state delta on '%s' : '%s' -> '%s'\n") 
                % f->first % f->second.first % fn->second->sha1sum);
              I(!fn->second->sha1sum.inner()().empty());
              safe_insert(cs.deltas_applied, std::make_pair(f->first, std::make_pair(f->second.first,fn->second->sha1sum)));
//              cvs_delta[path.as_internal()]=fn->second;
            }
#warning 2do mode_change ?
          // cs.attrs_cleared cs.attrs_set
          //if (fn->second.second->??
        }  
    }
  for (cvs_manifest::const_iterator f = newm.begin(); f != newm.end(); ++f)
    { mtn_automate::manifest_map::const_iterator mi=oldr.find(file_path_internal(f->first));
      if (mi==oldr.end())
      {  
        L(FL("adding file '%s' as '%s'\n") % f->second->sha1sum % f->first);
        I(!f->second->sha1sum.inner()().empty());
        file_path sp=file_path_internal(f->first);
        add_missing_parents(oldr, sp, cs);
        safe_insert(cs.files_added, make_pair(sp, f->second->sha1sum));
//        cvs_delta[f->first]=f->second;
        if (f->second->mode&0111)
          safe_insert(cs.attrs_set, std::make_pair(std::make_pair(sp, attr_key("mtn:execute")), attr_value("true")));
        if (f->second->keyword_substitution=="-kb")
          safe_insert(cs.attrs_set, std::make_pair(std::make_pair(sp, attr_key("mtn:manual_merge")), attr_value("true")));
      }
    }
}

static std::string time_t2monotone(const time_t &t)
{ struct tm *tm;
  tm=gmtime(&t);
  return (boost::format("%04d-%02d-%02dT%02d:%02d:%02d") % (tm->tm_year+1900) 
      % (tm->tm_mon+1) % tm->tm_mday % tm->tm_hour % tm->tm_min 
      % tm->tm_sec).str();
}

// commit CVS revisions to monotone (pull)
void cvs_repository::commit_cvs2mtn(std::set<cvs_edge>::iterator e)
{ revision_id parent_rid;
  
  cvs_edges_ticker.reset(0);
  L(FL("commit_revisions(%s %s)\n") % time_t2human(e->time) % e->revision);
  revision_ticker.reset(new ticker("revisions", "R", 3));
  if (e!=edges.begin())
  { std::set<cvs_edge>::const_iterator before=e;
    --before;
    L(FL("found last committed %s %s\n") % time_t2human(before->time) % before->revision);
    I(!before->revision.inner()().empty());
    parent_rid=before->revision;
  }
//  temp_node_id_source nis;
  for (; e!=edges.end(); ++e)
  { // roster_t new_roster=old_roster;
    // editable_roster_base eros(new_roster,nis);
    mtn_automate::cset cs;
    I(e->delta_base.inner()().empty()); // no delta yet
    cvs_manifest child_manifest=get_files(*e);
    L(FL("build_change_set(%s %s)\n") % time_t2human(e->time) % e->revision);
    // revision_set rev;
    // boost::shared_ptr<cset> cs(new cset());
    mtn_automate::manifest_map oldmanifest;
    if (!null_id(parent_rid))
      oldmanifest=app.get_manifest_of(parent_rid);
    build_change_set(*this,oldmanifest,e->xfiles,cs,remove_state);
    attach_sync_state(const_cast<cvs_sync::cvs_edge&>(*e),oldmanifest,cs);
    //cs->apply_to(eros);
    //calculate_ident(new_roster, rev.new_manifest);
    //safe_insert(rev.edges, std::make_pair(parent_rid, cs));
    
    if (!cs.is_nontrivial()) 
    { W(F("null edge (empty cs) @%s skipped\n") % time_t2human(e->time));
      continue;
    }
    if (e->xfiles.empty())
    { W(F("empty edge (no files) @%s skipped\n") % time_t2human(e->time));
      continue;
    }
#if 0
    if (child_map.empty())
    { W(F("empty edge (no files in manifest) @%s skipped\n") % time_t2human(e->time));
      // perhaps begin a new tree:
      // parent_rid=revision_id();
//      parent_manifest=cvs_manifest();
      continue;
    }
#endif
    revision_id child_rid=app.put_revision(parent_rid,cs);
    if (revision_ticker.get()) ++(*revision_ticker);
    L(FL("CVS Sync: Inserted revision %s into repository\n") % child_rid);
    e->revision=child_rid;

    app.cert_revision(child_rid,"branch",app.opts.branchname());
    std::string author=e->author;
    if (author.find('@')==std::string::npos) author+="@"+host;
    app.cert_revision(child_rid, "author", author); 
    app.cert_revision(child_rid, "changelog", e->changelog);
    app.cert_revision(child_rid, "date", time_t2monotone(e->time));
    parent_rid = child_rid;
  }
}

struct cvs_repository::prime_log_cb : rlog_callbacks
{ cvs_repository &repo;
  std::map<std::string,struct cvs_sync::file_history>::iterator i;
  time_t override_time;
  prime_log_cb(cvs_repository &r,const std::map<std::string,struct cvs_sync::file_history>::iterator &_i
          ,time_t overr_time=-1) 
      : repo(r), i(_i), override_time(overr_time) {}
  virtual void tag(const std::string &file,const std::string &tag, 
        const std::string &revision) const;
  virtual void revision(const std::string &file,time_t t,
        const std::string &rev,const std::string &author,
        const std::string &state,const std::string &log) const;
  virtual void file(const std::string &file,const std::string &head_rev) const
  { }
};

void cvs_repository::prime_log_cb::tag(const std::string &file,const std::string &tag, 
        const std::string &revision) const
{ MM(file);
  MM(tag);
  I(i->first==file);
  std::map<cvs_file_path,cvs_revision_nr> &tagslot=repo.tags[tag];
  tagslot[file]=revision;
  if (tag==repo.branch) repo.branch_point[file]=cvs_revision_nr(revision).get_branch_root();
}

static std::string app_signing_key="test@testdomain";

void cvs_repository::prime_log_cb::revision(const std::string &file,time_t checkin_time,
        const std::string &revision,const std::string &_author,
        const std::string &dead,const std::string &_message) const
{ L(FL("prime_log_cb %s:%s %s %s %d %s\n") % file % revision % time_t2human(checkin_time)
        % _author % _message.size() % dead);
  std::string author=_author;
  std::string message=_message;
  I(i->first==file);
  if (override_time!=-1)
  { checkin_time=override_time;
    message="initial state for cvs_pull --since";
    author=app_signing_key;
  }
  std::pair<std::set<file_state>::iterator,bool> iter=
    i->second.known_states.insert
      (file_state(checkin_time,revision,dead=="dead"));
  // I(iter.second==false);
  // set iterators are read only to prevent you from destroying the order
  file_state &fs=const_cast<file_state &>(*(iter.first));
  fs.log_msg=message;
  fs.author=author;
  std::pair<std::set<cvs_edge>::iterator,bool> iter2=
    repo.edges.insert(cvs_edge(message,checkin_time,author));
  if (iter2.second && repo.cvs_edges_ticker.get()) ++(*repo.cvs_edges_ticker);
}

void cvs_repository::prime()
{ retrieve_modules();
  get_all_files();
  revision_ticker.reset(0);
  cvs_edges_ticker.reset(new ticker("edges", "E", 10));
  for (std::map<std::string,file_history>::iterator i=files.begin();i!=files.end();++i)
  { std::vector<std::string> args;
    MM(i->first);
    
    if (!branch.empty())
    { args.push_back("-r"+branch);
      E(sync_since==-1, origin::user, F("--since does not work on a side branch"));
    }
    else 
      args.push_back("-b");
      
    if (sync_since!=-1) 
    { args.push_back("-d"); 
      size_t date_index=args.size();
      args.push_back(time_t2rfc822(sync_since));
      // state _at_ this point in time
      Log(prime_log_cb(*this,i,sync_since),i->first,args);
      // -d Jun 20 09:38:29 1997<
      args[date_index]+='<';
      // state _since_ this point in time
      Log(prime_log_cb(*this,i,sync_since),i->first,args);
    }
    else 
      Log(prime_log_cb(*this,i),i->first,args);
  }
  // remove duplicate states (because some edges were added by the 
  // get_all_files method)
  for (std::set<cvs_edge>::iterator i=edges.begin();i!=edges.end();)
  { if (i->changelog_valid || i->author.size()) { ++i; continue; }
    std::set<cvs_edge>::iterator j=i;
    j++;
    MM(boost::lexical_cast<std::string>(i->time));
    MM(boost::lexical_cast<std::string>(j->time));
    I(j!=edges.end());
    I(j->time==i->time);
    I(i->xfiles.empty());
    edges.erase(i);
    if (cvs_edges_ticker.get()) --(*cvs_edges_ticker);
    i=j; 
  }
  
  // join adjacent check ins (same author, same changelog)
  join_edge_parts(edges.begin());
  
  if (!branch_point.empty())
  { time_t root_time(0);
    // FIXME: look for this edge already in the database
    if (edges.begin()!=edges.end()) root_time=edges.begin()->time-1;
    std::set<cvs_edge>::iterator root_edge
     =edges.insert(cvs_edge(branch+" branching point",root_time,app_signing_key)).first;
    for (std::map<cvs_file_path,cvs_revision_nr>::const_iterator i=branch_point.begin();i!=branch_point.end();++i)
    { file_state fs(root_edge->time,i->second.get_string());
      fs.log_msg=root_edge->changelog;
      fs.author=root_edge->author;
      files[i->first].known_states.insert(fs);
    }
  }
  // since log already used Entry+Unchanged, reconnect to forget this states
  reconnect();
  
  // get the contents
  for (std::map<std::string,file_history>::iterator i=files.begin();i!=files.end();++i)
  { std::string file_contents;
    MM(i->first);
    I(!branch.empty() || !i->second.known_states.empty());
    if (!i->second.known_states.empty())
    { std::set<file_state>::iterator s2=i->second.known_states.begin();
      cvs_client::update c=Update(i->first,s2->cvs_version);
      store_checkout(s2,c,file_contents);
    }
    for (std::set<file_state>::iterator s=i->second.known_states.begin();
          s!=i->second.known_states.end();++s)
    { std::set<file_state>::iterator s2=s;
      ++s2;
      if (s2==i->second.known_states.end()) break;
      update(s,s2,i->first,file_contents);
    }
  }
  drop_connection();

  // fill in file states at given point
  fill_manifests(edges.begin());
  
  // commit them all
  if (!edges.empty()) commit_cvs2mtn(edges.begin());
  
//  store_modules();
}

struct cvs_client::checkout cvs_repository::CheckOut2(const std::string &file, const std::string &revision)
{ try
  { return CheckOut(file,revision);
  } catch (oops &e)
  { W(F("trying to reconnect, perhaps the server is confused\n"));
    reconnect();
    return CheckOut(file,revision);
  }
}

const cvs_manifest &cvs_repository::get_files(const revision_id &rid)
{ std::map<revision_id,std::set<cvs_edge>::iterator>::const_iterator
        cache_item=revision_lookup.find(rid);
  I(cache_item!=revision_lookup.end());
  return get_files(*(cache_item->second));
}

const cvs_manifest &cvs_repository::get_files(const cvs_edge &e)
{ L(FL("get_files(%s %s) %s %d\n") % time_t2human(e.time) % e.revision % e.delta_base % e.xfiles.size());
#if 0
  if (!e.delta_base.inner()().empty())
  { cvs_manifest calculated_manifest;
    // this is non-recursive by reason ...
    const cvs_edge *current=&e;
    std::vector<const cvs_edge *> deltas;
#if 0 // no longer delta encoded    
    while (!current->delta_base.inner()().empty())
    { L(FL("get_files: looking for base rev %s\n") % current->delta_base);
//      ++e.cm_delta_depth;
      deltas.push_back(current);
      std::map<revision_id,std::set<cvs_edge>::iterator>::const_iterator
        cache_item=revision_lookup.find(current->delta_base);
      E(cache_item!=revision_lookup.end(), 
          F("missing cvs cert on base revision %s\n") % current->delta_base);
      current=&*(cache_item->second);
    }
    I(current->delta_base.inner()().empty());
#endif
    calculated_manifest=current->xfiles;
    for (std::vector<const cvs_edge *>::const_reverse_iterator i=deltas.rbegin();
          i!=static_cast<std::vector<const cvs_edge *>::const_reverse_iterator>(deltas.rend());
          ++i)
      apply_manifest_delta(calculated_manifest,(*i)->xfiles);
    e.xfiles=calculated_manifest;
    e.delta_base=revision_id();
  }
#endif  
  return e.xfiles;
}

void cvs_sync::pull(const std::string &_repository, const std::string &_module,
            std::string const& _branch, mtncvs_state &app)
{ cvs_repository *repo=prepare_sync(_repository,_module,_branch,app);

  // initial checkout
  if (repo->empty()) 
    repo->prime();
  else repo->update();
  delete repo;
}

struct cvs_repository::update_cb : cvs_client::update_callbacks
{ cvs_repository &repo;
  std::vector<cvs_client::update> &results;
  
  update_cb(cvs_repository &r, std::vector<cvs_client::update> &re) 
  : repo(r), results(re) {}
  virtual void operator()(const cvs_client::update &u) const
  { results.push_back(u);
    // perhaps store the file contents into the db to save storage
  }
};

void cvs_repository::update()
{
  retrieve_modules();
  std::set<cvs_edge>::iterator now_iter=last_known_revision();
  const cvs_edge &now=*now_iter;
  I(!now.revision.inner()().empty());
  std::vector<update_args> file_revisions;
  std::vector<cvs_client::update> results;
  const cvs_manifest &m=get_files(now);
  file_revisions.reserve(m.size());
#warning FIXME: changed files
  for (cvs_manifest::const_iterator i=m.begin();i!=m.end();++i)
    file_revisions.push_back(update_args(i->first,i->second->cvs_version,
                            std::string(),i->second->keyword_substitution));
  Update(file_revisions,update_cb(*this,results));
  for (std::vector<cvs_client::update>::const_iterator i=results.begin();i!=results.end();++i)
  { // 2do: use tags
    cvs_manifest::const_iterator now_file=m.find(i->file);
    std::string last_known_revision;
    std::map<std::string,file_history>::iterator f=files.find(i->file);
    
    if (now_file!=m.end())
    { last_known_revision=now_file->second->cvs_version;
      I(f!=files.end());
    }
    else // the file is not present in our last import
    // e.g. the file is currently dead but we know an old revision
    { if (f!=files.end() // we know anything about this file
            && !f->second.known_states.empty()) // we have at least one known file revision
      { std::set<file_state>::const_iterator last=f->second.known_states.end();
        --last;
        last_known_revision=last->cvs_version;
      }
      else f=files.insert(std::make_pair(i->file,file_history())).first;
    }
    if (last_known_revision=="1.1.1.1") 
      last_known_revision="1.1";
    std::set<file_state>::const_iterator last=f->second.known_states.end();
    if (last!=f->second.known_states.begin()) --last;
    
    if (last_known_revision.empty())
      Log(prime_log_cb(*this,f),i->file.c_str(),"-b","-N",(void*)0);
    else
      // -b causes -r to get ignored on 0.12
      Log(prime_log_cb(*this,f),i->file.c_str(),/*"-b",*/"-N",
        ("-r"+last_known_revision+"::").c_str(),(void*)0);
    
    std::string file_contents,initial_contents;
    if(last==f->second.known_states.end() || last->dead)
    { last=f->second.known_states.begin();
      I(last!=f->second.known_states.end());
      std::set<file_state>::iterator s2=last;
      cvs_client::update c=Update(i->file,s2->cvs_version);
      store_checkout(s2,c,file_contents);
    }
    else
    { I(!last->sha1sum.inner()().empty());
      file_contents=app.get_file(last->sha1sum).inner()();
      initial_contents=file_contents;
    }
    for (std::set<file_state>::const_iterator s=last;
                  s!=f->second.known_states.end();++s)
    { std::set<file_state>::const_iterator s2=s;
      ++s2;
      if (s2==f->second.known_states.end()) break;
      if (s2->cvs_version==i->new_revision)
      { // we do not need to ask the host, we already did ...
        try
        { store_update(last,s2,*i,initial_contents);
        } catch (recoverable_failure &e)
        { W(F("error during update: %s\n") % e.what());
          // we _might_ try to use store delta ...
          cvs_client::update c=Update(i->file,s2->cvs_version);
          const_cast<std::string&>(s2->md5sum)="";
          const_cast<unsigned&>(s2->patchsize)=0;
          store_contents(file_data(c.contents,origin::internal), const_cast<file_id&>(s2->sha1sum));
          const_cast<unsigned&>(s2->size)=c.contents.size();
          const_cast<std::string&>(s2->keyword_substitution)=c.keyword_substitution;
        }
        break;
      }
      else
      { update(s,s2,i->file,file_contents);
      }
    }
  }
  drop_connection();
  
  std::set<cvs_edge>::iterator dummy_iter=now_iter;
  ++dummy_iter;
  if (dummy_iter!=edges.end())
  {
    join_edge_parts(dummy_iter);
    fill_manifests(dummy_iter);
    if (global_sanity.debug_p()) L(FL("%s") % debug());
    commit_cvs2mtn(dummy_iter);
  }
//  store_modules();
}

