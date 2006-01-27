// copyright (C) 2005-2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "cvs_sync.hh"
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

#ifdef WIN32
#define sleep(x) _sleep(x)
#endif

using namespace std;

// since the piece methods in rcs_import depend on rcs_file I cannot reuse them
// I rely on string handling reference counting (which is not that bad IIRC)
//  -> investigate under which conditions a string gets copied
static std::string const cvs_cert_name="cvs-revisions";

using namespace cvs_sync;

bool file_state::operator<(const file_state &b) const
{ return since_when<b.since_when
    || (since_when==b.since_when 
        && cvs_revision_nr(cvs_version)<cvs_revision_nr(b.cvs_version));
}

// whether time is below span or (within span and lesser author,changelog)
bool cvs_sync::operator<(const file_state &s,const cvs_edge &e)
{ return s.since_when<e.time ||
    (s.since_when<=e.time2 && (s.author<e.author ||
    (s.author==e.author && s.log_msg<e.changelog)));
}

// whether time is below span or (within span and lesser/equal author,changelog)
bool 
cvs_sync::operator<=(const file_state &s,const cvs_edge &e)
{ return s.since_when<e.time ||
    (s.since_when<=e.time2 && (s.author<=e.author ||
    (s.author==e.author && s.log_msg<=e.changelog)));
}

/* supported by the woody version:
Root Valid-responses valid-requests Repository Directory Max-dotdot
Static-directory Sticky Checkin-prog Update-prog Entry Kopt Checkin-time
Modified Is-modified UseUnchanged Unchanged Notify Questionable Case
Argument Argumentx Global_option Gzip-stream wrapper-sendme-rcsOptions Set
expand-modules ci co update diff log rlog add remove update-patches
gzip-file-contents status rdiff tag rtag import admin export history release
watch-on watch-off watch-add watch-remove watchers editors annotate
rannotate noop version
*/

//--------------------- implementation -------------------------------

size_t const cvs_edge::cvs_window;

bool cvs_revision_nr::operator==(const cvs_revision_nr &b) const
{ return parts==b.parts;
}

// is this strictly correct? feels ok for now (and this is last ressort)
bool cvs_revision_nr::operator<(const cvs_revision_nr &b) const
{ return parts<b.parts;
}

cvs_revision_nr::cvs_revision_nr(const std::string &x)
{ std::string::size_type begin=0;
  do
  { std::string::size_type end=x.find(".",begin);
    std::string::size_type len=end-begin;
    if (end==std::string::npos) len=std::string::npos;
    parts.push_back(atoi(x.substr(begin,len).c_str()));
    begin=end;
    if (begin!=std::string::npos) ++begin;
  } while(begin!=std::string::npos);
};

// we cannot guess whether the revision following 1.3 is 1.3.2.1 or 1.4 :-(
// so we can only hope, that this is the expected result
void cvs_revision_nr::operator++()
{ if (parts.empty()) return;
  if (parts.size()==4 && get_string()=="1.1.1.1") *this=cvs_revision_nr("1.2");
  else parts.back()++;
}

std::string cvs_revision_nr::get_string() const
{ std::string result;
  for (std::vector<int>::const_iterator i=parts.begin();i!=parts.end();++i)
  { if (!result.empty()) result+=".";
    result+=boost::lexical_cast<string>(*i);
  }
  return result;
}

bool cvs_revision_nr::is_parent_of(const cvs_revision_nr &child) const
{ unsigned cps=child.parts.size();
  unsigned ps=parts.size();
  if (cps<ps) 
  { if (child==cvs_revision_nr("1.2") && *this==cvs_revision_nr("1.1.1.1"))
      return true;
    return false;
  }
  if (is_branch() || child.is_branch()) return false;
  unsigned diff=0;
  for (;diff<ps;++diff) if (child.parts[diff]!=parts[diff]) break;
  if (cps==ps)
  { if (diff+1!=cps) return false;
    if (parts[diff]+1 != child.parts[diff]) return false;
  }
  else // ps < cps
  { if (diff!=ps) return false;
    if (ps+2!=cps) return false;
    if (child.parts[diff]&1 || !child.parts[diff]) return false;
    if (child.parts[diff+1]!=1) return false;
  }
  return true;
}

// impair number of numbers => branch tag
bool cvs_revision_nr::is_branch() const 
{ return parts.size()&1;
}

cvs_revision_nr cvs_revision_nr::get_branch_root() const
{ I(parts.size()>=4); 
  I(!(parts.size()&1)); // even number of digits
  I(!parts[parts.size()-2]); // but last digit is zero
  I(!(parts[parts.size()-1]&1)); // last digit is even
  cvs_revision_nr result;
  result.parts=std::vector<int>(parts.begin(),parts.end()-2);
  return result;
}

// cvs_repository ----------------------

// very short form to output in logs etc.
std::string time_t2human(const time_t &t)
{ struct tm *tm;
  tm=gmtime(&t);
  return (boost::format("%02d%02d%02dT%02d%02d%02d") % (tm->tm_year%100) 
      % (tm->tm_mon+1) % tm->tm_mday % tm->tm_hour % tm->tm_min 
      % tm->tm_sec).str();
}

struct cvs_repository::get_all_files_log_cb : rlog_callbacks
{ cvs_repository &repo;
  get_all_files_log_cb(cvs_repository &r) : repo(r) {}
  virtual void file(const std::string &file,const std::string &head_rev) const
  { L(FL("get_all_files_log_cb %s") % file);
    repo.files[file]; 
  }
  virtual void tag(const std::string &file,const std::string &tag, 
        const std::string &revision) const {}
  virtual void revision(const std::string &file,time_t t,
        const std::string &rev,const std::string &author,
        const std::string &state,const std::string &log) const {}
};

// get all available files and their newest revision
void cvs_repository::get_all_files()
{ if (edges.empty())
  { // rlist seems to be more efficient but it's hard to guess the directory the
    // server talks about
    I(CommandValid("rlog"));
    RLog(get_all_files_log_cb(*this),false,"-N","-h","--",module.c_str(),(void*)0);
  }
}

std::string debug_manifest(const cvs_manifest &mf)
{ std::string result;
  for (cvs_manifest::const_iterator i=mf.begin(); i!=mf.end(); ++i)
  { result+= i->first + " " + i->second->cvs_version;
    if (!i->second->keyword_substitution.empty()) 
      result+="/"+i->second->keyword_substitution;
    result+=" " + std::string(i->second->dead?"dead ":"") + i->second->sha1sum() + "\n";
  }
  return result;
}

std::string cvs_repository::debug_file(std::string const& name)
{ std::map<std::string,file_history>::const_iterator i=files.find(name);
  E(i!=files.end(),F("file '%s' not found\n") % name);
  std::string result;
  for (std::set<file_state>::const_iterator j=i->second.known_states.begin();
        j!=i->second.known_states.end();++j)
  { result+="since "+time_t2human(j->since_when);
    result+=" V"+j->cvs_version+" ";
    if (j->dead) result+= "dead";
    else if (j->size) result+= boost::lexical_cast<string>(j->size);
    else if (j->patchsize) result+= "p" + boost::lexical_cast<string>(j->patchsize);
    else if (!j->sha1sum().empty()) result+= j->sha1sum().substr(0,4) + j->keyword_substitution;
    result+=" "+j->log_msg.substr(0,20)+"\n";
  }
  return result;
}

#if 0
std::string debug_files(const std::map<std::string,file_history> &files)
{ std::string result;
  for (std::map<std::string,file_history>::const_iterator i=files.begin();
      i!=files.end();++i)
  { result += i->first;
    result += " (";
    for (std::set<file_state>::const_iterator j=i->second.known_states.begin();
          j!=i->second.known_states.end();)
    { result +=  boost::lexical_cast<string>(j->since_when%1000) + ":" + j->cvs_version + "=";
      if (j->dead) result +=  "dead";
      else if (j->size) result +=  boost::lexical_cast<string>(j->size);
      else if (j->patchsize) result +=  'p' + boost::lexical_cast<string>(j->patchsize);
      else if (!j->sha1sum().empty()) result +=  j->sha1sum().substr(0,4) + j->keyword_substitution;
      ++j;
      if (j!=i->second.known_states.end()) result += ",";
    }
    result += ")\n";
  }
  return result;
}
#endif

// returns the length of the first line (header) and fills in fields
std::string::size_type cvs_repository::parse_cvs_cert_header(cert_value const& value, 
      std::string &repository, std::string& module, std::string& branch)
{ 
  MM(value());
  std::string::size_type nlpos=value().find('\n');
  E(nlpos!=std::string::npos, F("malformed cvs-revision cert %s") % value());
  std::string repo=value().substr(0,nlpos);
  std::string::size_type modulebegin=repo.find('\t'); 
  E(modulebegin!=std::string::npos, F("malformed cvs-revision header %s") % repo);
  std::string::size_type branchbegin=repo.find(modulebegin,'\t');
  std::string::size_type modulelen=std::string::npos;
  
  if (branchbegin!=std::string::npos)
  { branch=repo.substr(branchbegin+1);
    modulelen=branchbegin-modulebegin-1;
  }
  repository=repo.substr(0,modulebegin);
  module=repo.substr(modulebegin+1,modulelen);
  return nlpos;
}

void cvs_repository::parse_cvs_cert_header(revision<cert> const& c, 
      std::string &repository, std::string& module, std::string& branch)
{
  cert_value value;
  decode_base64(c.inner().value, value);
  parse_cvs_cert_header(value, repository, module, branch);
}

std::string cvs_repository::create_cvs_cert_header() const
{ 
  // I assume that at least TAB is uncommon in path names - even on Windows
  std::string result=host+":"+root+"\t"+module;
  if (!branch.empty()) result+="\t"+branch;
  return result+"\n";
}

std::string cvs_repository::debug() const
{ std::string result;

  // edges set<cvs_edge>
  result+= "Edges :\n";
  for (std::set<cvs_edge>::const_iterator i=edges.begin();
      i!=edges.end();++i)
  { result+= "[" + time_t2human(i->time);
    if (i->time!=i->time2) result+= "+" + boost::lexical_cast<string>(i->time2-i->time);
    if (!i->revision().empty()) result+= "," + i->revision().substr(0,4);
    if (!i->xfiles.empty()) 
      result+= "," + boost::lexical_cast<string>(i->xfiles.size()) 
         + (i->delta_base.inner()().empty()?"files":"deltas");
    result+= "," + i->author + ",";
    std::string::size_type nlpos=i->changelog.find_first_of("\n\r");
    if (nlpos>50) nlpos=50;
    result+= i->changelog.substr(0,nlpos) + "]\n";
  }
  result+= "Files :\n";
  for (std::map<std::string,file_history>::const_iterator i=files.begin();
      i!=files.end();++i)
  { result+= i->first;
    result+= " (";
    for (std::set<file_state>::const_iterator j=i->second.known_states.begin();
          j!=i->second.known_states.end();)
    { if (j->dead) result+= "dead";
      else if (j->size) result+= boost::lexical_cast<string>(j->size);
      else if (j->patchsize) result+= "p" + boost::lexical_cast<string>(j->patchsize);
      else if (!j->sha1sum().empty()) result+= j->sha1sum().substr(0,4) + j->keyword_substitution;
      ++j;
      if (j!=i->second.known_states.end()) result+= ",";
    }
    result+= ")\n";
  }
  result+= "Tags :\n";
  for (std::map<std::string,std::map<std::string,cvs_revision_nr> >::const_iterator i=tags.begin();
      i!=tags.end();++i)
  { result+= i->first + "(" + boost::lexical_cast<string>(i->second.size()) + " files)\n";
  }
  return result;
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
    author=repo.app.signing_key();
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

bool cvs_edge::similar_enough(cvs_edge const & other) const
{
  if (changelog != other.changelog)
    return false;
  if (author != other.author)
    return false;
  if (labs(time - other.time) > cvs_window
      && labs(time2 - other.time) > cvs_window)
    return false;
  return true;
}

bool cvs_edge::operator<(cvs_edge const & other) const
{
  return time < other.time ||

    (time == other.time 
     && author < other.author) ||

    (time == other.time 
     && author == other.author 
     && changelog < other.changelog);
}

void cvs_repository::store_contents(const data &dat, hexenc<id> &sha1sum)
{
  calculate_ident(dat,sha1sum);
  if (!app.db.file_version_exists(sha1sum))
  { app.db.put_file(sha1sum, dat);
    if (file_id_ticker.get()) ++(*file_id_ticker);
  }
}

static void apply_delta(piece::piece_table &contents, const std::string &patch)
{ piece::piece_table after;
  piece::apply_diff(contents,after,patch);
  std::swap(contents,after);
}

void cvs_repository::store_delta(const std::string &new_contents, 
          const std::string &old_contents, 
          // this argument is unused since we can no longer use the rcs patch
          const std::string &dummy, 
          const hexenc<id> &from, hexenc<id> &to)
{ if (old_contents.empty())
  { store_contents(new_contents, to);
    return;
  }
  data dat(new_contents);
  calculate_ident(dat,to);
  if (!app.db.file_version_exists(to))
  { delta del;
    diff(data(old_contents), data(new_contents), del);
    if (dat().size()<=del().size())
    // the data is smaller or of equal size to the patch
      app.db.put_file(to, dat);
    else 
      app.db.put_file_version(from,to,del);
    if (file_id_ticker.get()) ++(*file_id_ticker);
  }
}

static
void add_missing_parents(roster_t const& oldr, split_path const & sp, boost::shared_ptr<cset> cs)
{ split_path tmp;
  std::string log;
  dump(sp,log);
  L(FL("add_missing_parents(,%s,)\n") % log);
  for (split_path::const_iterator i=sp.begin();i!=sp.end() && i!=--sp.end();++i)
  { L(FL("path comp '%s'/%d\n") % *i % sp.size());
    tmp.push_back(*i);
    // already added?
    if (cs->dirs_added.find(tmp)!=cs->dirs_added.end()) continue;
    if (!oldr.has_node(tmp)) 
    { L(FL("adding directory %s\n") % file_path(tmp));
      safe_insert(cs->dirs_added, tmp);
    }
  }
}

// compare the new manifest with the old roster and fill the cset accordingly
static bool 
build_change_set(const cvs_client &c, roster_t const& oldr, cvs_manifest &newm,
                 boost::shared_ptr<cset> cs, cvs_file_state const& remove_state, 
                 unsigned cm_delta_depth)
{
  cvs_manifest cvs_delta;
  
  node_map const & nodes = oldr.all_nodes();
  L(FL("build_change_set(%d,%d,)\n") % nodes.size() % newm.size());
  
  for (node_map::const_iterator f = nodes.begin(); f != nodes.end(); ++f)
    { node_t node = f->second;
      split_path sp;
      oldr.get_name(f->first, sp);
      file_path path(sp);
      
      if (is_dir_t(node)) continue;
      
      cvs_manifest::const_iterator fn = newm.find(path.as_internal());
      if (fn==newm.end())
      {  
        L(FL("deleting file '%s'\n") % path);
        safe_insert(cs->nodes_deleted, sp);
        cvs_delta[path.as_internal()]=remove_state;
      }
      else 
        { file_t file = downcast_to_file_t(node);
          if (file->content == fn->second->sha1sum)
            {
//              L(FL("skipping preserved entry state '%s' on '%s'\n")
//                % fn->second->sha1sum % fn->first);         
            }
          else
            {
              L(FL("applying state delta on '%s' : '%s' -> '%s'\n") 
                % path % file->content % fn->second->sha1sum);
              I(!fn->second->sha1sum().empty());
              safe_insert(cs->deltas_applied, make_pair(sp, make_pair(file->content,fn->second->sha1sum)));
              cvs_delta[path.as_internal()]=fn->second;
            }
#warning 2do mode_change
          // cs->attrs_cleared cs->attrs_set
        }  
    }
  for (cvs_manifest::const_iterator f = newm.begin(); f != newm.end(); ++f)
    {
      split_path sp;
      file_path_internal(f->first).split(sp);
      if (!oldr.has_node(sp))
      {  
        L(FL("adding file '%s' as '%s'\n") % f->second->sha1sum % f->first);
        I(!f->second->sha1sum().empty());
        split_path sp;
        file_path_internal(f->first).split(sp);
        add_missing_parents(oldr, sp, cs);
        safe_insert(cs->files_added, make_pair(sp, f->second->sha1sum));
        cvs_delta[f->first]=f->second;
      }
    }
  if (!nodes.empty() && cvs_delta.size()<newm.size() 
      && cm_delta_depth+1<cvs_edge::cm_max_delta_depth)
  { newm=cvs_delta;
    return true;
  }
  return false;
}

void cvs_repository::check_split(const cvs_file_state &s, const cvs_file_state &end, 
          const std::set<cvs_edge>::iterator &e)
{ cvs_file_state s2=s;
  ++s2;
  if (s2==end) return;
  MM(boost::lexical_cast<std::string>(s->since_when));
  MM(boost::lexical_cast<std::string>(s2->since_when));
  I(s->since_when!=s2->since_when);
  // checkins must not overlap (next revision must lie beyond edge)
  if ((*s2) <= (*e))
  { W(F("splitting edge %s-%s at %s\n") % time_t2human(e->time) 
        % time_t2human(e->time2) % time_t2human(s2->since_when));
    cvs_edge new_edge=*e;
    MM(boost::lexical_cast<std::string>(e->time));
    I(s2->since_when-1>=e->time);
    e->time2=s2->since_when-1;
    new_edge.time=s2->since_when;
    edges.insert(new_edge);
  }
}

void cvs_repository::join_edge_parts(std::set<cvs_edge>::iterator i)
{ for (;i!=edges.end();)
  { std::set<cvs_edge>::iterator j=i;
    j++; // next one
    if (j==edges.end()) break;
    
    MM(boost::lexical_cast<std::string>(j->time2));
    MM(boost::lexical_cast<std::string>(j->time));
    MM(boost::lexical_cast<std::string>(i->time2));
    MM(boost::lexical_cast<std::string>(i->time));
    I(j->time2==j->time); // make sure we only do this once
    I(i->time2<=j->time); // should be sorted ...
    if (!i->similar_enough(*j)) 
    { ++i; continue; }
    I((j->time-i->time2)<=time_t(cvs_edge::cvs_window)); // just to be sure
    I(i->author==j->author);
    I(i->changelog==j->changelog);
    I(i->time2<j->time); // should be non overlapping ...
    L(FL("joining %s-%s+%s\n") % time_t2human(i->time) % time_t2human(i->time2) % time_t2human(j->time));
    i->time2=j->time;
    edges.erase(j);
  }
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
    std::string md5sum=xform<Botan::Hex_Decoder>(u.checksum);
    I(md5sum.size()==hash.OUTPUT_LENGTH);
    Botan::SecureVector<Botan::byte> hashval=hash.process(contents);
    I(hashval.size()==hash.OUTPUT_LENGTH);
    unsigned hashidx=hash.OUTPUT_LENGTH;
    for (;hashidx && hashval[hashidx-1]==Botan::byte(md5sum[hashidx-1]);--hashidx) ;
    if (!hashidx)
    { store_delta(contents, old_contents, u.patch, s->sha1sum, const_cast<hexenc<id>&>(s2->sha1sum));
    }
    else
    { E(false, F("MD5 sum %s<>%s") % u.checksum 
          % xform<Botan::Hex_Encoder>(std::string(hashval.begin(),hashval.end())));
    }
  }
  else
  { if (!s->sha1sum().empty()) 
    // we default to patch if it's at all possible
      store_delta(u.contents, contents, std::string(), s->sha1sum, const_cast<hexenc<id>&>(s2->sha1sum));
    else
      store_contents(u.contents, const_cast<hexenc<id>&>(s2->sha1sum));
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
    store_contents(c.contents, const_cast<hexenc<id>&>(s2->sha1sum));
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
    } catch (informative_failure &e)
    { W(F("Update: patching failed with %s\n") % e.what);
      cvs_client::update c=Update(file,s2->cvs_version);
      if (c.mod_time!=s2->since_when && c.mod_time!=-1 && s2->since_when!=sync_since)
      { W(F("checkout time %s and log time %s disagree\n") % time_t2human(c.mod_time) % time_t2human(s2->since_when));
      }
      const_cast<std::string&>(s2->md5sum)="";
      const_cast<unsigned&>(s2->patchsize)=0;
      store_contents(c.contents, const_cast<hexenc<id>&>(s2->sha1sum));
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
      store_contents(c.contents, const_cast<hexenc<id>&>(s2->sha1sum));
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
    store_contents(c.contents, const_cast<hexenc<id>&>(s2->sha1sum));
    const_cast<unsigned&>(s2->size)=c.contents.size();
    file_contents=c.contents;
    const_cast<std::string&>(s2->keyword_substitution)=c.keyword_substitution;
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
    store_contents(c.contents, const_cast<hexenc<id>&>(s2->sha1sum));
    const_cast<unsigned&>(s2->size)=c.contents.size();
    file_contents=c.contents;
    const_cast<std::string&>(s2->keyword_substitution)=c.keyword_substitution;
  }
}

void cvs_repository::fill_manifests(std::set<cvs_edge>::iterator e)
{ cvs_manifest current_manifest;
  if (e!=edges.begin())
  { std::set<cvs_edge>::const_iterator before=e;
    --before;
    current_manifest=get_files(*before);
  }
  for (;e!=edges.end();++e)
  { std::set<cvs_edge>::iterator next_edge=e;
    ++next_edge;
    for (std::map<std::string,file_history>::const_iterator f=files.begin();f!=files.end();++f)
    { I(!branch.empty() || !f->second.known_states.empty());
      // this file does not belong to this branch
      if (f->second.known_states.empty()) continue; 
      if (!(*(f->second.known_states.begin()) <= (*e)))
      // the file does not exist yet (first is not below/equal current edge)
      { L(FL("%s before beginning %s/%s+%d\n") % f->first 
            % time_t2human(f->second.known_states.begin()->since_when)
            % time_t2human(e->time) % (e->time2-e->time));
        continue; 
      }
      cvs_manifest::iterator mi=current_manifest.find(f->first);
      if (mi==current_manifest.end()) // the file is currently dead
      { cvs_file_state s=f->second.known_states.end();
        // find last revision that fits but does not yet belong to next edge
        // use until end, or above range, or belongs to next edge
        for (cvs_file_state s2=f->second.known_states.begin();
             s2!=f->second.known_states.end() 
             && (*s2)<=(*e)
             && ( next_edge==edges.end() || ((*s2)<(*next_edge)) );
             ++s2)
        { L(FL("%s matches %s/%s+%d\n") % f->first 
            % time_t2human(s2->since_when)
            % time_t2human(e->time) % (e->time2-e->time));
          s=s2;
        }
          
        if (s!=f->second.known_states.end() && !s->dead)
        // a matching revision was found
        { current_manifest[f->first]=s;
          I(!s->sha1sum().empty());
          check_split(s,f->second.known_states.end(),e);
        }
      }
      else // file was present in last manifest, check whether next revision already fits
      { cvs_file_state s=mi->second;
        ++s;
        if (s!=f->second.known_states.end() 
            && (*s)<=(*e)
            && ( next_edge==edges.end() || ((*s)<(*next_edge)) ) )
        { if (s->dead) current_manifest.erase(mi);
          else 
          { mi->second=s;
            I(!s->sha1sum().empty());
          }
          check_split(s,f->second.known_states.end(),e);
        }
      }
    }
    e->xfiles=current_manifest;
  }
}

// commit CVS revisions to monotone (pull)
void cvs_repository::commit_cvs2mtn(std::set<cvs_edge>::iterator e)
{ revision_id parent_rid;
  roster_t old_roster;
  packet_db_writer dbw(app);
  unsigned cm_delta_depth=0;
  
  cvs_edges_ticker.reset(0);
  L(FL("commit_revisions(%s %s)\n") % time_t2human(e->time) % e->revision());
  revision_ticker.reset(new ticker("revisions", "R", 3));
  if (e!=edges.begin())
  { std::set<cvs_edge>::const_iterator before=e;
    --before;
    L(FL("found last committed %s %s\n") % time_t2human(before->time) % before->revision());
    I(!before->revision().empty());
    parent_rid=before->revision;
    app.db.get_roster(parent_rid, old_roster);
    cm_delta_depth=before->cm_delta_depth;
  }
  temp_node_id_source nis;
  for (; e!=edges.end(); ++e)
  { roster_t new_roster=old_roster;
    editable_roster_base eros(new_roster,nis);
    I(e->delta_base.inner()().empty()); // no delta yet
    cvs_manifest child_manifest=get_files(*e);
    L(FL("build_change_set(%s %s)\n") % time_t2human(e->time) % e->revision());
#warning cm_delta_depth kann ganz weg, wenn auf Dateien umgestellt
    revision_set rev;
    boost::shared_ptr<cset> cs(new cset());
    if (build_change_set(*this,old_roster,e->xfiles,cs,remove_state,cm_delta_depth))
    { e->delta_base=parent_rid;
      e->cm_delta_depth=cm_delta_depth+1;
    }
    cs->apply_to(eros);
    calculate_ident(new_roster, rev.new_manifest);
    safe_insert(rev.edges, std::make_pair(parent_rid, cs));
    revision_id child_rid;
    calculate_ident(rev, child_rid);
    
    if (!rev.is_nontrivial()) 
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
    L(FL("CVS Sync: Inserting revision %s (%s) into repository\n") % child_rid % rev.new_manifest);
    e->revision=child_rid.inner();
    if (!app.db.revision_exists(child_rid))
    { app.db.put_revision(child_rid, rev);
      if (revision_ticker.get()) ++(*revision_ticker);
    }
    cert_revision_in_branch(child_rid, app.branch_name(), app, dbw); 
    std::string author=e->author;
    if (author.find('@')==std::string::npos) author+="@"+host;
    cert_revision_author(child_rid, author, app, dbw); 
    cert_revision_changelog(child_rid, e->changelog, app, dbw);
    cert_revision_date_time(child_rid, e->time, app, dbw);
    cert_cvs(*e, dbw);

    parent_rid = child_rid;
    old_roster=new_roster;
    cm_delta_depth=e->cm_delta_depth;
  }
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
      N(sync_since==-1, F("--since does not work on a side branch"));
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
     =edges.insert(cvs_edge(branch+" branching point",root_time,repo.app.signing_key())).second;
    for (std::map<cvs_file_path,cvs_revision_nr>::const_iterator i=branch_point.begin();i!=branch_point.end();++i)
    { file_state fs(root_edge->time,i->second.get_string());
      fs.log_msg=root_edge->changelog;
      fs.autorh=root_edge->author;
      files[i->first].known_states.insert(fs);
    }
  }
  
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
  
  store_modules();
}

void cvs_repository::cert_cvs(const cvs_edge &e, packet_consumer & pc)
{ 
  std::string content=create_cvs_cert_header();
  if (!e.delta_base.inner()().empty()) 
  { content+="+"+e.delta_base.inner()()+"\n";
  }
  for (cvs_manifest::const_iterator i=e.xfiles.begin(); i!=e.xfiles.end(); ++i)
  { if (i->second->cvs_version.empty())
    { W(F("blocking attempt to certify an empty CVS revision\n"
        "(this is normal for a cvs_takeover of a locally modified tree)\n"));
      return;
    }
    content+=i->second->cvs_version;
    if (!i->second->keyword_substitution.empty())
      content+="/"+i->second->keyword_substitution;
    content+=" "+i->first+"\n";
  }
  cert t;
  make_simple_cert(e.revision, cert_name(cvs_cert_name), content, app, t);
  revision<cert> cc(t);
  pc.consume_revision_cert(cc);
}

cvs_repository::cvs_repository(app_state &_app, const std::string &repository, 
            const std::string &module, const std::string &branch, bool connect)
      : cvs_client(repository,module,branch,connect), app(_app), 
        file_id_ticker(), revision_ticker(), cvs_edges_ticker(), 
        remove_state(), sync_since(-1)
{
  file_id_ticker.reset(new ticker("file ids", "F", 10));
  remove_state=remove_set.insert(file_state(0,"-",true)).first;
  if (!app.sync_since().empty())
  { sync_since=posix2time_t(app.sync_since());
    N(sync_since<=time(0), F("Since lies in the future. Remember to specify time in UTC\n"));
  }
}

static void test_key_availability(app_state &app)
{
  // early short-circuit to avoid failure after lots of work
  rsa_keypair_id key;
  get_user_key(key, app);
  // Require the password early on, so that we don't do lots of work
  // and then die.
  app.signing_key = key;

  N(app.lua.hook_persist_phrase_ok(),
    F("need permission to store persistent passphrase (see hook persist_phrase_ok())"));
  require_password(key, app);
}

std::set<cvs_edge>::iterator cvs_repository::last_known_revision()
{ I(!edges.empty());
  std::set<cvs_edge>::iterator now_iter=edges.end();
  --now_iter;
  return now_iter;
}

time_t cvs_repository::posix2time_t(std::string posix_format)
{ std::string::size_type next_illegal=0;
  MM(posix_format);
  while ((next_illegal=posix_format.find_first_of("-:"))!=std::string::npos)
        posix_format.erase(next_illegal,1);
  boost::posix_time::ptime tmp= boost::posix_time::from_iso_string(posix_format);
  boost::posix_time::time_duration dur= tmp
          -boost::posix_time::ptime(boost::gregorian::date(1970,1,1),
                          boost::posix_time::time_duration(0,0,0,0));
  return dur.total_seconds();
}

cvs_edge::cvs_edge(const revision_id &rid, app_state &app)
 : changelog_valid(), time(), time2(), cm_delta_depth()
{ revision=hexenc<id>(rid.inner());
  // get author + date 
  std::vector< ::revision<cert> > edge_certs;
  app.db.get_revision_certs(rid,edge_certs);
  // erase_bogus_certs ?
  for (std::vector< ::revision<cert> >::const_iterator c=edge_certs.begin();
            c!=edge_certs.end();++c)
  { cert_value value;
    decode_base64(c->inner().value, value);   
    if (c->inner().name()==date_cert_name)
    { L(FL("date cert %s\n")%value());
      time=time2=cvs_repository::posix2time_t(value());
    }
    else if (c->inner().name()==author_cert_name)
    { author=value();
    }
    else if (c->inner().name()==changelog_cert_name)
    { changelog=value();
      changelog_valid=true;
    }
  }
}

std::set<cvs_edge>::iterator cvs_repository::commit_mtn2cvs(
      std::set<cvs_edge>::iterator parent, const revision_id &rid, bool &fail)
{ // check that it's the last one
  L(FL("commit %s -> %s\n") % parent->revision % rid);
  { std::set<cvs_edge>::iterator test=parent;
    ++test;
    I(test==edges.end());
  }
  // a bit like process_certs
  cvs_edge e(rid,app);

  revision_set rs;
  app.db.get_revision(rid, rs);
  std::vector<commit_arg> commits;
  unsigned cm_delta_depth=parent->cm_delta_depth;
  
  for (edge_map::const_iterator j = rs.edges.begin(); 
       j != rs.edges.end();
       ++j)
  { if (!(edge_old_revision(j) == parent->revision)) 
    { L(FL("%s != %s\n") % edge_old_revision(j) % parent->revision);
      continue;
    }
    boost::shared_ptr<cset> cs=j->second;
    cvs_manifest parent_manifest=get_files(*parent);

    for (path_set::const_iterator i=cs->nodes_deleted.begin();
            i!=cs->nodes_deleted.end(); ++i)
    { commit_arg a;
      a.file=file_path(*i).as_internal();
      cvs_manifest::const_iterator old=parent_manifest.find(a.file);
      I(old!=parent_manifest.end());
      a.removed=true;
      a.old_revision=old->second->cvs_version;
      a.keyword_substitution=old->second->keyword_substitution;
      commits.push_back(a);
      L(FL("delete %s -%s %s\n") % a.file % a.old_revision % a.keyword_substitution);
    }

    for (std::map<split_path,split_path>::const_iterator i
                            =cs->nodes_renamed.begin();
            i!=cs->nodes_renamed.end(); ++i)
    { commit_arg a; // remove
      a.file=file_path(i->first).as_internal();
      cvs_manifest::const_iterator old=parent_manifest.find(a.file);
      I(old!=parent_manifest.end());
      a.removed=true;
      a.old_revision=old->second->cvs_version;
      a.keyword_substitution=old->second->keyword_substitution;
      commits.push_back(a);
      L(FL("rename from %s -%s %s\n") % a.file % a.old_revision % a.keyword_substitution);
      
      a=commit_arg(); // add
      a.file=file_path(i->second).as_internal();
      I(!old->second->sha1sum().empty());
      file_data dat;
      app.db.get_file_version(old->second->sha1sum,dat);
      a.new_content=dat.inner()();
      commits.push_back(a);
      L(FL("rename to %s %d\n") % a.file % a.new_content.size());
    }
    
    for (path_set::const_iterator i=cs->dirs_added.begin();
            i!=cs->dirs_added.end(); ++i)
    { std::string name=file_path(*i).as_internal();
      L(FL("dir add %s\n") % name);
#warning create directory
    }

    for (std::map<split_path, file_id>::const_iterator 
            i=cs->files_added.begin();
            i!=cs->files_added.end(); ++i)
    { 
      commit_arg a;
      a.file=file_path(i->first).as_internal();
      file_data dat;
      app.db.get_file_version(i->second,dat);
      a.new_content=dat.inner()();
      commits.push_back(a);
      L(FL("add %s %d\n") % a.file % a.new_content.size());
    }

    for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator 
            i=cs->deltas_applied.begin();
            i!=cs->deltas_applied.end(); ++i)
    { 
      commit_arg a;
      a.file=file_path(i->first).as_internal();
      cvs_manifest::const_iterator old=parent_manifest.find(a.file);
      I(old!=parent_manifest.end());
      a.old_revision=old->second->cvs_version;
      a.keyword_substitution=old->second->keyword_substitution;
      file_data dat;
      app.db.get_file_version(i->second.second,dat);
      a.new_content=dat.inner()();
      commits.push_back(a);
      L(FL("delta %s %s %s %d\n") % a.file % a.old_revision % a.keyword_substitution
          % a.new_content.size());
    }

    if (commits.empty())
    { W(F("revision %s: nothing to commit") % e.revision());
          packet_db_writer dbw(app);
      e.cm_delta_depth=++cm_delta_depth;
      e.delta_base=parent->revision;
      cert_cvs(e, dbw);
      revision_lookup[e.revision]=edges.insert(e).first;
      fail=false;
      return --(edges.end());
    }
    std::string changelog;
    changelog=e.changelog+"\nmonotone "+e.author+" "
        +cvs_client::time_t2rfc822(e.time)+" "+e.revision()+"\n";
    // gather information CVS does not know about into the changelog
    changelog+=gather_merge_information(e.revision);
    std::map<std::string,std::pair<std::string,std::string> > result
      =Commit(changelog,e.time,commits);
    if (result.empty()) { fail=true; return edges.end(); }
    
    e.delta_base=parent->revision;
    
    // the result of the commit: create history entry (file state)
    //    FIXME: is this really necessary?
    for (std::map<std::string,std::pair<std::string,std::string> >::const_iterator
            i=result.begin(); i!=result.end(); ++i)
    { if (i->second.first.empty())
      { e.xfiles[i->first]=remove_state;
      }
      else
      { file_state fs(e.time,i->second.first);
        fs.log_msg=e.changelog;
        fs.author=e.author;
        fs.keyword_substitution=i->second.second;
        split_path sp;
        file_path_internal(i->first).split(sp);
        std::map<split_path, std::pair<file_id, file_id> >::const_iterator mydelta=cs->deltas_applied.find(sp);
        if (mydelta!=cs->deltas_applied.end())
        { fs.sha1sum=mydelta->second.second.inner();
        }
        else // newly added?
        { std::map<split_path, file_id>::const_iterator myadd=cs->files_added.find(sp);
          I(myadd!=cs->files_added.end());
          fs.sha1sum=myadd->second.inner();
        }
        std::pair<std::set<file_state>::iterator,bool> newelem=
            files[i->first].known_states.insert(fs);
        I(newelem.second);
        e.xfiles[i->first]=newelem.first;
      }
    }
    packet_db_writer dbw(app);
    if (cm_delta_depth+1>=cvs_edge::cm_max_delta_depth) 
    { get_files(e);
      cm_delta_depth=0;
    }
    else
      e.cm_delta_depth=++cm_delta_depth;
    cert_cvs(e, dbw);
    revision_lookup[e.revision]=edges.insert(e).first;
    if (global_sanity.debug) L(FL("%s") % debug());
    fail=false;
    return --(edges.end());
  }
  W(F("no matching parent found\n"));
  fail=true;
  return edges.end();
}

std::string cvs_repository::gather_merge_information(revision_id const& id)
{ std::set<revision_id> parents;
  app.db.get_revision_parents(id,parents);
  std::string result;
  for (std::set<revision_id>::const_iterator i=parents.begin();i!=parents.end();++i)
  { std::vector< revision<cert> > certs;
    if (*i==revision_id()) continue;
    app.db.get_revision_certs(*i,certs);
    std::vector< revision<cert> >::const_iterator j=certs.begin();
    std::string to_match=create_cvs_cert_header();
    for (;j!=certs.end();++j)
    { if (j->inner().name()!=cvs_cert_name) continue;
      cert_value value;
      decode_base64(j->inner().value, value);
      if (value().size()<to_match.size()) continue;
      if (value().substr(0,to_match.size())!=to_match) continue;
      break;
    }
    // this revision is already in _this_ repository
    if (j!=certs.end()) continue;
    
    std::string author,changelog;
    time_t date=0;
    for (j=certs.begin();j!=certs.end();++j)
    { cert_value value;
      decode_base64(j->inner().value, value);   
      if (j->inner().name()==date_cert_name)
      { date=cvs_repository::posix2time_t(value());
      }
      else if (j->inner().name()==author_cert_name)
      { author=value();
      }
      else if (j->inner().name()==changelog_cert_name)
      { changelog=value();
      }
    }
    result+="-------------------\n"
        +changelog+"\nmonotone "+author+" "
        +cvs_client::time_t2rfc822(date)+" "+i->inner()()+"\n";
    result+=gather_merge_information(*i);
  }
  return result;
}

void cvs_repository::commit()
{ retrieve_modules();
  std::set<cvs_edge>::iterator now_iter=last_known_revision();
  while (now_iter!=edges.end())
  { const cvs_edge &now=*now_iter;
    I(!now.revision().empty());
    
    L(FL("looking for children of revision %s\n") % now.revision);
    std::set<revision_id> children;
    app.db.get_revision_children(now.revision, children);
    
    if (!app.branch_name().empty())
    { base64<cert_value> value;
      encode_base64(cert_value(app.branch_name()), value);
      // ignore revisions not belonging to the specified branch
      for (std::set<revision_id>::iterator i=children.begin();
                    i!=children.end();)
      { std::vector< revision<cert> > certs;
        app.db.get_revision_certs(*i,branch_cert_name,value,certs);
        std::set<revision_id>::iterator help=i;
        ++help;
        if (certs.empty()) children.erase(i);
        i=help;
      }
    }
    if (children.empty()) return;
    revision_id next;
    if (children.size()>1) // && !ap.revision_selectors.size())
    { for (std::vector<utf8>::const_iterator i=app.revision_selectors.begin();
          i!=app.revision_selectors.end();++i)
      { for (std::set<revision_id>::const_iterator j=children.begin();
          j!=children.end();++j)
        { if (revision_id(hexenc<id>((*i)()))==*j)
          { next=*j;
            break;
          }
        }
      }
      if (next.inner()().empty())
      { W(F("several children found for %s:\n") % now.revision);
        for (std::set<revision_id>::const_iterator i=children.begin();
                    i!=children.end();++i)
        { W(F("%s\n") % *i);
        }
        W(F("please specify direction using --revision\n"));
        return;
      }
    }
    else next=*children.begin();
    bool fail=bool();
    now_iter=commit_mtn2cvs(now_iter,next,fail);
    
    if (!fail)
      P(F("checked %s into cvs repository") % now.revision);
    // we'd better seperate the commits so that ordering them is possible
    if (now_iter!=edges.end()) sleep(2);
  }
  store_modules();
}

// look for _any_ cvs cert in the given monotone branch and assign
// its value to repository, module, branch

// this is somewhat clumsy ... but works well enough
static void guess_repository(std::string &repository, std::string &module,
        std::string & branch,
        std::vector< revision<cert> > &certs, app_state &app)
{ I(!app.branch_name().empty());
  app.db.get_revision_certs(cvs_cert_name, certs); 
  // erase_bogus_certs ?
  std::vector< revision<cert> > branch_certs;
  base64<cert_value> branch_value;
  encode_base64(cert_value(app.branch_name()), branch_value);
  app.db.get_revision_certs(branch_cert_name, branch_value, branch_certs);
  // use a set to gain speed? scan the smaller vector to gain speed?
  for (std::vector< revision<cert> >::const_iterator ci=certs.begin();
        ci!=certs.end();++ci)
    for (std::vector< revision<cert> >::const_iterator bi=branch_certs.begin();
          bi!=branch_certs.end();++bi)
    { // actually this finds an arbitrary element of the set intersection
      if (ci->inner().ident==bi->inner().ident)
      { try
        { cvs_repository::parse_cvs_cert_header(*ci,repository,module,branch);
          if (branch.empty())
            L(FL("using module '%s' in repository '%s'\n") % module % repository);
          else
            L(FL("using branch '%s' of module '%s' in repository '%s'\n") 
                % branch % module % repository);
          goto break_outer;
        }
        catch (std::exception &e)
        { W(F("exception %s on revision %s\n") % e.what() % ci->inner().ident);
        }
        catch (informative_failure &e)
        { W(F("exception %s on revision %s\n") % e.what % ci->inner().ident);
        }
      }
    }
  break_outer: ;
  N(!module.empty(), F("No cvs cert in this branch, please specify repository and module"));
}

void cvs_sync::push(const std::string &_repository, const std::string &_module,
            std::string const& _branch, app_state &app)
{ test_key_availability(app);
  // make the variables changeable
  std::string repository=_repository, module=_module, branch=_branch;
  std::vector< revision<cert> > certs;
  if (repository.empty() || module.empty())
    guess_repository(repository, module, branch, certs, app);
  cvs_sync::cvs_repository repo(app,repository,module,branch);
// turned off for DEBUGGING
  if (!getenv("CVS_CLIENT_LOG"))
    repo.GzipStream(3);
  transaction_guard guard(app.db);

  if (certs.empty())
    app.db.get_revision_certs(cvs_cert_name, certs);
  repo.process_certs(certs);
  
  N(!repo.empty(),
    F("no revision certs for this repository/module\n"));

  repo.commit();
  
  guard.commit();      
}

void cvs_sync::pull(const std::string &_repository, const std::string &_module,
            std::string const& _branch, app_state &app)
{ test_key_availability(app);
  // make the variables changeable
  std::string repository=_repository, module=_module, branch=_branch;
  
  std::vector< revision<cert> > certs;

  if (repository.empty() || module.empty())
    guess_repository(repository, module, branch, certs, app);
  cvs_sync::cvs_repository repo(app,repository,module,branch);
// turn compression on when not DEBUGGING
  if (!getenv("CVS_CLIENT_LOG"))
    repo.GzipStream(3);
  transaction_guard guard(app.db);

  if (certs.empty()) app.db.get_revision_certs(cvs_cert_name, certs); 
  if (!app.cvspull_full) repo.process_certs(certs);
  
  // initial checkout
  if (repo.empty()) 
    repo.prime();
  else repo.update();
  
  guard.commit();      
}

cvs_file_state cvs_repository::remember(std::set<file_state> &s,const file_state &fs, std::string const& filename)
{ for (std::set<file_state>::iterator i=s.begin();i!=s.end();++i)
  { if (i->cvs_version==fs.cvs_version)
    { if (i->since_when>fs.since_when) // i->since_when has to be the minimum
        const_cast<time_t&>(i->since_when)=fs.since_when;
      static file_id emptysha1sum;
      if (emptysha1sum.inner()().empty())
        calculate_ident(data(),const_cast<hexenc<id>&>(emptysha1sum.inner()));
      if (i->log_msg=="last cvs update (modified)" 
            && i->sha1sum==emptysha1sum.inner()
            && i->author==("unknown@"+host))
      { W(F("replacing fake contents for %s V%s\n")
            % filename % i->cvs_version);
        const_cast<hexenc<id>&>(i->sha1sum)=fs.sha1sum;
        const_cast<std::string&>(i->log_msg)=fs.log_msg;
      }
      return i;
    }
  }
  std::pair<cvs_file_state,bool> iter=s.insert(fs);
  I(iter.second);
  return iter.first;
}

void cvs_repository::process_certs(const std::vector< revision<cert> > &certs)
{ 
  std::auto_ptr<ticker> cert_ticker;
  cert_ticker.reset(new ticker("cvs certs", "C", 10));

  std::string needed_cert=create_cvs_cert_header();
  for (vector<revision<cert> >::const_iterator i=certs.begin(); i!=certs.end(); ++i)
  { // populate data structure using these certs
    cert_value cvs_revisions;
    decode_base64(i->inner().value, cvs_revisions);
    if (cvs_revisions().size()>needed_cert.size() 
      && (cvs_revisions().substr(0,needed_cert.size())==needed_cert))
    { // parse and add the cert
      ++(*cert_ticker);
      cvs_edge e(i->inner().ident,app);

      piece::piece_table pieces;
      // in Zeilen aufteilen
      piece::index_deltatext(cvs_revisions(),pieces);
      I(!pieces.empty());
      piece::piece_table::const_iterator p=pieces.begin()+1;
      if ((**p)[0]=='+') // this is a delta encoded manifest
      { hexenc<id> h=(**p).substr(1,40); // remember to omit the trailing \n
        e.delta_base=revision_id(h);
        ++p;
      }
      for (;p!=pieces.end();++p)
      { std::string line=**p;
        I(!line.empty());
        I(line[line.size()-1]=='\n');
        line.erase(line.size()-1,1);
        // the format is "<revsion>[/<keyword_substitution>] <path>\n"
        // e.g. "1.1 .cvsignore",     "1.43/-kb test.png"
        std::string::size_type space=line.find(' ');
        I(space!=std::string::npos);
        std::string monotone_path=line.substr(space+1);
        std::string path=monotone_path;
        // look for the optional initial slash separating the keyword mode
        std::string::size_type slash=line.find('/');
        if (slash==std::string::npos || slash>space)
          slash=space;

        file_state fs;
        fs.since_when=e.time;
        fs.cvs_version=line.substr(0,slash);
        if (space!=slash) 
          fs.keyword_substitution=line.substr(slash+1,space-(slash+1));
        if (fs.cvs_version=="-") // delta encoded: remove
        { I(!e.delta_base.inner()().empty());
          fs.log_msg=e.changelog;
          fs.author=e.author;
          fs.dead=true;
          cvs_file_state cfs=remember(files[path].known_states,fs,path);
          e.xfiles.insert(std::make_pair(path,cfs)); // remove_state));
        }
        else
        { // get sha1sum of file
          roster_t roster;
          app.db.get_roster(i->inner().ident, roster);
          split_path sp;
          file_path_internal(monotone_path).split(sp);
          node_t node=roster.get_node(sp);
          file_t file = downcast_to_file_t(node);
          fs.sha1sum=file->content.inner();

          fs.log_msg=e.changelog;
          fs.author=e.author;
          cvs_file_state cfs=remember(files[path].known_states,fs,path);
          e.xfiles.insert(std::make_pair(path,cfs));
        }
      }
      piece::reset();
      revision_lookup[e.revision]=edges.insert(e).first;
    }
    else L(FL("cvs cert %s ignored (!=%s)") % cvs_revisions % needed_cert);
  }
  // because some manifests might have been absolute (not delta encoded)
  // we possibly did not notice removes. check for them
  std::set<cvs_edge>::const_iterator last=edges.end();
  for (std::set<cvs_edge>::const_iterator i=edges.begin();
      i!=edges.end();++i)
  { if (last!=edges.end() && i->delta_base.inner()().empty())
      try
      { cvs_manifest old=get_files(*last),new_m=get_files(*i);
        for (cvs_manifest::iterator j=old.begin();j!=old.end();++j)
        { cvs_manifest::iterator new_iter=new_m.find(j->first);
          if (new_iter==new_m.end()) // this file get's removed here
          { file_state fs;
            fs.since_when=i->time;
            cvs_revision_nr rev=j->second->cvs_version;
            ++rev;
            fs.cvs_version=rev.get_string();
            fs.log_msg=i->changelog;
            fs.author=i->author;
            fs.dead=true;
            L(FL("file %s gets removed at %s\n") % j->first % i->revision());
            remember(files[j->first].known_states,fs,j->first);
          }
        }
      }
      catch (informative_failure &e)
      { L(FL("failed to reconstruct CVS revisions: %s: %s->%s\n")
            % e.what % last->revision() % i->revision());
      }
    last=i;
  }
  if (global_sanity.debug) L(FL("%s") % debug());
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
{ retrieve_modules();
  std::set<cvs_edge>::iterator now_iter=last_known_revision();
  const cvs_edge &now=*now_iter;
  I(!now.revision().empty());
  std::vector<update_args> file_revisions;
  std::vector<cvs_client::update> results;
  const cvs_manifest &m=get_files(now);
  file_revisions.reserve(m.size());
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
    { I(!last->sha1sum().empty());
      file_data dat;
      app.db.get_file_version(last->sha1sum,dat);
      file_contents=dat.inner()();
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
        } catch (informative_failure &e)
        { W(F("error during update: %s\n") % e.what);
          // we _might_ try to use store delta ...
          cvs_client::update c=Update(i->file,s2->cvs_version);
          const_cast<std::string&>(s2->md5sum)="";
          const_cast<unsigned&>(s2->patchsize)=0;
          store_contents(c.contents, const_cast<hexenc<id>&>(s2->sha1sum));
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
    if (global_sanity.debug) L(FL("%s") % debug());
    commit_cvs2mtn(dummy_iter);
  }
  
  store_modules();
}

static void apply_manifest_delta(cvs_manifest &base,const cvs_manifest &delta)
{ L(FL("apply_manifest_delta: base %d delta %d\n") % base.size() % delta.size());
  for (cvs_manifest::const_iterator i=delta.begin(); i!=delta.end(); ++i)
  { if (i->second->dead)
    { cvs_manifest::iterator to_remove=base.find(i->first);
      I(to_remove!=base.end());
      base.erase(to_remove);
    }
    else
      base[i->first]=i->second;
  }
  L(FL("apply_manifest_delta: result %d\n") % base.size());
}

const cvs_manifest &cvs_repository::get_files(const cvs_edge &e)
{ L(FL("get_files(%s %s) %s %d\n") % time_t2human(e.time) % e.revision % e.delta_base % e.xfiles.size());
  if (!e.delta_base.inner()().empty())
  { cvs_manifest calculated_manifest;
    // this is non-recursive by reason ...
    const cvs_edge *current=&e;
    std::vector<const cvs_edge *> deltas;
    while (!current->delta_base.inner()().empty())
    { L(FL("get_files: looking for base rev %s\n") % current->delta_base);
      ++e.cm_delta_depth;
      deltas.push_back(current);
      std::map<revision_id,std::set<cvs_edge>::iterator>::const_iterator
        cache_item=revision_lookup.find(current->delta_base);
      E(cache_item!=revision_lookup.end(), 
          F("missing cvs cert on base revision %s\n") % current->delta_base);
      current=&*(cache_item->second);
    }
    I(current->delta_base.inner()().empty());
    calculated_manifest=current->xfiles;
    for (std::vector<const cvs_edge *>::const_reverse_iterator i=deltas.rbegin();
          i!=static_cast<std::vector<const cvs_edge *>::const_reverse_iterator>(deltas.rend());
          ++i)
      apply_manifest_delta(calculated_manifest,(*i)->xfiles);
    e.xfiles=calculated_manifest;
    e.delta_base=revision_id();
  }
  return e.xfiles;
}

void cvs_sync::debug(const std::string &command, const std::string &arg, 
            app_state &app)
{ 
  // we default to the first repository found (which might not be what you wanted)
  if (command=="manifest" && arg.size()==constants::idlen)
  { revision_id rid(arg);
    // easy but not very efficient way, since we parse all revisions to decode
    // the delta encoding
    // (perhaps?) it would be better to retrieve needed revisions recursively
    std::vector< revision<cert> > certs;
    app.db.get_revision_certs(rid,cvs_cert_name,certs);
    // erase_bogus_certs ?
    N(!certs.empty(),F("revision has no 'cvs-revisions' certificates\n"));
    
    std::string repository,module,branch;
    cvs_repository::parse_cvs_cert_header(certs.front(), repository, module, branch);
    cvs_sync::cvs_repository repo(app,repository,module,branch,false);
    app.db.get_revision_certs(cvs_cert_name, certs);
    repo.process_certs(certs);
    std::cout << debug_manifest(repo.get_files(rid));
  }
  else if (command=="history") // filename or empty
  { 
    std::string repository, module, branch;
    std::vector< revision<cert> > certs;
    guess_repository(repository, module, branch, certs, app);
    cvs_sync::cvs_repository repo(app,repository,module,branch,false);
    repo.process_certs(certs);
    if (arg.empty()) std::cout << repo.debug();
    else std::cout << repo.debug_file(arg);
  }
}

const cvs_manifest &cvs_repository::get_files(const revision_id &rid)
{ std::map<revision_id,std::set<cvs_edge>::iterator>::const_iterator
        cache_item=revision_lookup.find(rid);
  I(cache_item!=revision_lookup.end());
  return get_files(*(cache_item->second));
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

void cvs_client::validate_path(const std::string &local, const std::string &server)
{ for (std::map<std::string,std::string>::const_iterator i=server_dir.begin();
      i!=server_dir.end();++i)
  { if (local.substr(0,i->first.size())==i->first 
        && server.substr(0,i->second.size())==i->second
        && local.substr(i->first.size())==server.substr(i->second.size()))
      return;
  }
  server_dir[local]=server;
}

void cvs_repository::takeover_dir(const std::string &path)
{ // remember the server path for this subdirectory
  MM(path);
  { std::string repository;
    std::ifstream cvs_repository((path+"CVS/Repository").c_str());
    N(cvs_repository.good(), F("can't open %sCVS/Repository\n") % path);
    std::getline(cvs_repository,repository);
    I(!repository.empty());
    if (repository[0]!='/') repository=root+"/"+repository;
    validate_path(path,repository+"/");
  }
  std::ifstream cvs_Entries((path+"CVS/Entries").c_str());
  N(cvs_Entries.good(),
      F("can't open %s\n") % (path+"CVS/Entries"));
  L(FL("takeover_dir %s\n") % path);
  static hexenc<id> empty_file;
  while (true)
  { std::string line;
    std::getline(cvs_Entries,line);
    if (!cvs_Entries.good()) break;
    if (!line.empty())
    { std::vector<std::string> parts;
      MM(line);
      stringtok(parts,line,"/");
      // empty last part will not get created
      if (parts.size()==5) parts.push_back(std::string());
      if (parts.size()!=6) 
      { W(F("entry line with %d components '%s'\n") % parts.size() %line);
        continue;
      }
      if (parts[0]=="D")
      { takeover_dir(path+parts[1]+"/");
      }
      else // file
      {  // remember permissions, store file contents
        I(parts[0].empty());
        std::string filename=path+parts[1];
        I(!access(filename.c_str(),R_OK));
        // parts[2]=version
        // parts[3]=date
        // parts[4]=keyword subst
        // parts[5]='T'tag
        time_t modtime=-1;
        try
        { modtime=cvs_client::Entries2time_t(parts[3]);
        } catch (std::exception &e) {}
        catch (informative_failure &e) {}
        I(files.find(filename)==files.end());
        std::map<std::string,file_history>::iterator f
            =files.insert(std::make_pair(filename,file_history())).first;
        file_state fs(modtime,parts[2]);
        fs.author="unknown";
        fs.keyword_substitution=parts[4];
        { struct stat sbuf;
          I(!stat(filename.c_str(), &sbuf));
          if (sbuf.st_mtime!=modtime)
          { L(FL("modified %s %u %u\n") % filename % modtime % sbuf.st_mtime);
            fs.log_msg="partially overwritten content from last update";
            store_contents(std::string(), fs.sha1sum);
            f->second.known_states.insert(fs);
            
            fs.since_when=time(NULL);
            fs.cvs_version=std::string();
          }
        }
        // @@ import the file and check whether it is (un-)changed
        fs.log_msg="initial cvs content";
        data new_data;
        read_localized_data(file_path_internal(filename), new_data, app.lua);
        store_contents(new_data, fs.sha1sum);
        f->second.known_states.insert(fs);
      }
    }
  }
}

void cvs_repository::takeover()
{ takeover_dir("");
  
  bool need_second=false;
  cvs_edge e1,e2;
  e1.time=0;
  e1.changelog="last cvs update (modified)";
  e1.changelog_valid=true;
  e1.author="unknown";
  e2.time=time(NULL);
  e2.changelog="cvs takeover";
  e2.changelog_valid=true;
  e2.author="unknown";
  for (std::map<std::string,file_history>::const_iterator i=files.begin();
      i!=files.end();++i)
  { cvs_file_state first,second;
    first=i->second.known_states.begin();
    I(first!=i->second.known_states.end());
    second=first;
    ++second;
    if (second==i->second.known_states.end()) second=first;
    else if (!need_second) need_second=true;
    if (e1.time<first->since_when) e1.time=first->since_when;
    e1.xfiles[i->first]=first;
    e2.xfiles[i->first]=second;
    // at most two states known !
    I((++second)==i->second.known_states.end());
  }
  if (!need_second) e1.changelog=e2.changelog;
  edges.insert(e1);
  if (need_second)
  { edges.insert(e2);
  }
  // commit them all
  commit_cvs2mtn(edges.begin());
  // create a MT directory 
  app.create_workspace(system_path("."));
  
// like in commit ?
//  update_any_attrs(app);
  put_revision_id((--edges.end())->revision);
//  maybe_update_inodeprints(app);

  store_modules();
}

// read in directory put into db
void cvs_sync::takeover(app_state &app, const std::string &_module)
{ std::string root,module=_module,branch;

  N(access("MT",F_OK),F("Found a MT file or directory, already under monotone's control?"));
  { fstream cvs_root("CVS/Root");
    N(cvs_root.good(),
      F("can't open ./CVS/Root, please change into the working directory\n"));
    std::getline(cvs_root,root);
  }
  { fstream cvs_branch("CVS/Tag");
    if (cvs_branch.good())
    { std::getline(cvs_branch,branch);
      MM(branch);
      I(!branch.empty());
      I(branch[0]=='T');
      branch.erase(0,1);
    }
  }
  if (module.empty())
  { fstream cvs_repository("CVS/Repository");
    N(cvs_repository.good(),
      F("can't open ./CVS/Repository\n"));
    std::getline(cvs_repository,module);
    W(F("Guessing '%s' as the module name\n") % module);
  }
  test_key_availability(app);
  cvs_sync::cvs_repository repo(app,root,module,branch,false);
  // FIXME? check that directory layout matches the module structure
  repo.takeover();
}

void cvs_repository::store_modules()
{ const std::map<std::string,std::string> &sd=GetServerDir();
  std::string value;
  std::string name=create_cvs_cert_header();
  for (std::map<std::string,std::string>::const_iterator i=sd.begin();
        i!=sd.end();++i)
  { value+=i->first+"\t"+i->second+"\n";
  }
  std::pair<var_domain,var_name> key(var_domain("cvs-server-path"), var_name(name));
  var_value oldval;
  try 
  { app.db.get_var(key,oldval);
  } catch (...) {}
  if (oldval()!=value) app.db.set_var(key, value);
}

void cvs_repository::retrieve_modules()
{ if (!GetServerDir().empty()) return;
  std::string name=create_cvs_cert_header();
  std::pair<var_domain,var_name> key(var_domain("cvs-server-path"), var_name(name));
  var_value value;
  try {
    app.db.get_var(key,value);
  } catch (...) { return; }
  std::map<std::string,std::string> sd;
  piece::piece_table pieces;
  std::string value_s=value();
  piece::index_deltatext(value_s,pieces);
  for (piece::piece_table::const_iterator p=pieces.begin();p!=pieces.end();++p)
  { std::string line=**p;
    MM(line);
    I(!line.empty());
    std::string::size_type tab=line.find('\t');
    I(tab!=std::string::npos);
    I(line[line.size()-1]=='\n');
    line.erase(line.size()-1,1);
    sd[line.substr(0,tab)]=line.substr(tab+1);
  }
  piece::reset();
  SetServerDir(sd);
}
