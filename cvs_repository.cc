// copyright (C) 2005 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "cvs_sync.hh"
#include "keys.hh"
#include "transforms.hh"
#include <vector>
#include <boost/lexical_cast.hpp>
#include "cryptopp/md5.h"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/greg_date.hpp>

using namespace std;

// since the piece methods in rcs_import depend on rcs_file I cannot reuse them
// I rely on string handling reference counting (which is not that bad IIRC)
//  -> investigate under which conditions a string gets copied
namespace cvs_sync
{
static std::string const cvs_cert_name="cvs-revisions";

struct 
piece
{
  piece(string::size_type p, string::size_type l, const std::string &_s) :
    pos(p), len(l), s(_s) {}
  string::size_type pos;
  string::size_type len;
  string s;
  const string operator*() const
  { return s.substr(pos,len); }
};


static void 
index_deltatext(std::string const & dt, vector<piece> & pieces);
static void 
process_one_hunk(vector< piece > const & source,
                 vector< piece > & dest,
                 vector< piece >::const_iterator & i,
                 int & cursor);
static void 
build_string(vector<piece> const & pieces, string & out);
static void
construct_version(vector< piece > const & source_lines,
                  vector< piece > & dest_lines,
                  string const & deltatext);
}

using namespace cvs_sync;

std::ostream &operator<<(std::ostream &o, const file_state &f)
{ return o << f.since_when << ' ' << f.cvs_version << ' ' << f.dead;
}

static void cvs_sync::process_one_hunk(vector< piece > const & source,
                 vector< piece > & dest,
                 vector< piece >::const_iterator & i,
                 int & cursor)
{
  string directive = **i;
  assert(directive.size() > 1);
  ++i;

  char code;
  int pos, len;
  sscanf(directive.c_str(), " %c %d %d", &code, &pos, &len);

  try 
    {
      if (code == 'a')
        {
          // 'ax y' means "copy from source to dest until cursor == x, then
          // copy y lines from delta, leaving cursor where it is"
          while (cursor < pos)
            dest.push_back(source.at(cursor++));
          I(cursor == pos);
          while (len--)
            dest.push_back(*i++);
        }
      else if (code == 'd')
        {      
          // 'dx y' means "copy from source to dest until cursor == x-1,
          // then increment cursor by y, ignoring those y lines"
          while (cursor < (pos - 1))
            dest.push_back(source.at(cursor++));
          I(cursor == pos - 1);
          cursor += len;
        }
      else 
        throw oops("unknown directive '" + directive + "'");
    } 
  catch (std::out_of_range & oor)
    {
      throw oops("std::out_of_range while processing " + directive 
                 + " with source.size() == " 
                 + boost::lexical_cast<string>(source.size())
                 + " and cursor == "
                 + boost::lexical_cast<string>(cursor));
    }  
}

static void 
cvs_sync::build_string(vector<piece> const & pieces, string & out)
{
  out.clear();
  out.reserve(pieces.size() * 60);
  for(vector<piece>::const_iterator i = pieces.begin();
      i != pieces.end(); ++i)
    out.append(i->s, i->pos, i->len);
}

static void 
cvs_sync::index_deltatext(std::string const & dt, vector<piece> & pieces)
{
  pieces.clear();
  pieces.reserve(dt.size() / 30);  
  string::size_type begin = 0;
  string::size_type end = dt.find('\n');
  while(end != string::npos)
    {
      // nb: the piece includes the '\n'
      pieces.push_back(piece(begin, (end - begin) + 1, dt));
      begin = end + 1;
      end = dt.find('\n', begin);
    }
  if (begin != dt.size())
    {
      // the text didn't end with '\n', so neither does the piece
      end = dt.size();
      pieces.push_back(piece(begin, end - begin, dt));
    }
}

static void
cvs_sync::construct_version(vector< piece > const & source_lines,
                  vector< piece > & dest_lines,
                  string const & deltatext)
{
  dest_lines.clear();
  dest_lines.reserve(source_lines.size());

  vector<piece> deltalines;
  index_deltatext(deltatext, deltalines);
  
  int cursor = 0;
  for (vector<piece>::const_iterator i = deltalines.begin(); 
       i != deltalines.end(); )
    process_one_hunk(source_lines, dest_lines, i, cursor);
  while (cursor < static_cast<int>(source_lines.size()))
    dest_lines.push_back(source_lines[cursor++]);
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

void cvs_revision_nr::operator++(int)
{ if (parts.empty()) return;
  parts.back()++;
}

std::string cvs_revision_nr::get_string() const
{ std::string result;
  for (std::vector<int>::const_iterator i=parts.begin();i!=parts.end();)
  { result+= (F("%d") % *i).str();
    ++i;
    if (i!=parts.end()) result+",";
  }
  return result;
}

bool cvs_revision_nr::is_parent_of(const cvs_revision_nr &child) const
{ unsigned cps=child.parts.size();
  unsigned ps=parts.size();
  if (cps<ps) return false;
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

// cvs_repository ----------------------

#if 0
void cvs_repository::ticker() const
{ cvs_client::ticker(false);
  if (revisions_created)
    std::cerr << "[new revisions: " << revisions_created << "] ";
  else if (files_inserted) std::cerr << "[new file ids: " << files_inserted << "] ";
  else std::cerr << "[files: " << files.size() << "] ";
  std::cerr << "[edges: " << edges.size() << "] "
      "[tags: "  << tags.size() << "]\n";
}
#endif

struct cvs_repository::get_all_files_log_cb : rlog_callbacks
{ cvs_repository &repo;
  get_all_files_log_cb(cvs_repository &r) : repo(r) {}
  virtual void file(const std::string &file,const std::string &head_rev) const
  { repo.files[file]; }
  virtual void tag(const std::string &file,const std::string &tag, 
        const std::string &revision) const {}
  virtual void revision(const std::string &file,time_t t,
        const std::string &rev,const std::string &author,
        const std::string &state,const std::string &log) const {}
};

struct cvs_repository::get_all_files_list_cb : rlist_callbacks
{ cvs_repository &repo;
  get_all_files_list_cb(cvs_repository &r) : repo(r) {}
  virtual void file(const std::string &name, time_t last_change,
        const std::string &last_rev, bool dead) const
  { repo.files[name].known_states.insert(file_state(last_change,last_rev,dead));
    repo.edges.insert(cvs_edge(last_change));
  }
};

// get all available files and their newest revision
void cvs_repository::get_all_files()
{ if (edges.empty())
  { if (CommandValid("rlist"))
    { RList(get_all_files_list_cb(*this),false,"-l","-R","-d","--",module.c_str(),0);
    }
    else // less efficient? ...
    { I(CommandValid("rlog"));
      RLog(get_all_files_log_cb(*this),false,"-N","-h","--",module.c_str(),0);
    }
  }
}

void cvs_repository::debug() const
{ // edges set<cvs_edge>
  std::cerr << "Edges :\n";
  for (std::set<cvs_edge>::const_iterator i=edges.begin();
      i!=edges.end();++i)
  { std::cerr << "[" << i->time;
    if (i->time!=i->time2) std::cerr << '+' << (i->time2-i->time);
    if (!i->files.empty()) std::cerr << ',' << i->files.size() << "files";
    std::cerr << ',' << i->author << ',';
    std::string::size_type nlpos=i->changelog.find_first_of("\n\r");
    if (nlpos>50) nlpos=50;
    std::cerr << i->changelog.substr(0,nlpos) << "]\n";
  }
//  std::cerr << '\n';
  // files map<string,file_history>
  std::cerr << "Files :\n";
  for (std::map<std::string,file_history>::const_iterator i=files.begin();
      i!=files.end();++i)
  { std::cerr << shorten_path(i->first);
    std::cerr << "(";
    for (std::set<file_state>::const_iterator j=i->second.known_states.begin();
          j!=i->second.known_states.end();)
    { if (j->dead) std::cerr << "dead";
      else if (j->size) std::cerr << j->size;
      else if (j->patchsize) std::cerr << 'p' << j->patchsize;
      else if (!j->sha1sum().empty()) std::cerr << j->sha1sum().substr(0,3);
      ++j;
      if (j!=i->second.known_states.end()) std::cerr << ',';
    }
    std::cerr << ")\n";
  }
//  std::cerr << '\n';
  // tags map<string,map<string,string> >
  std::cerr << "Tags :\n";
  for (std::map<std::string,std::map<std::string,std::string> >::const_iterator i=tags.begin();
      i!=tags.end();++i)
  { std::cerr << i->first << "(" << i->second.size() << " files)\n";
  }
//  std::cerr << '\n';
}

struct cvs_repository::prime_log_cb : rlog_callbacks
{ cvs_repository &repo;
  std::map<std::string,struct cvs_sync::file_history>::iterator i;
  prime_log_cb(cvs_repository &r,const std::map<std::string,struct cvs_sync::file_history>::iterator &_i) 
      : repo(r), i(_i) {}
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
{ I(i->first==file);
  std::map<std::string,std::string> &tagslot=repo.tags[tag];
  tagslot[file]=revision;
}

void cvs_repository::prime_log_cb::revision(const std::string &file,time_t checkin_time,
        const std::string &revision,const std::string &author,
        const std::string &dead,const std::string &message) const
{ I(i->first==file);
  std::pair<std::set<file_state>::iterator,bool> iter=
    i->second.known_states.insert
      (file_state(checkin_time,revision,dead=="dead"));
  // I(iter.second==false);
  // set iterators are read only to prevent you from destroying the order
  file_state &fs=const_cast<file_state &>(*(iter.first));
  fs.log_msg=message;
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

void cvs_repository::store_contents(const std::string &contents, hexenc<id> &sha1sum)
{
  data dat(contents);
  calculate_ident(dat,sha1sum);
  if (!app.db.file_version_exists(sha1sum))
  { base64<gzip<data> > packed;
    pack(dat, packed);
    file_data fdat=packed;
    app.db.put_file(sha1sum, fdat);
    if (file_id_ticker.get()) ++(*file_id_ticker);
  }
}

static void apply_delta(vector<piece> &contents, const std::string &patch)
{ vector<piece> after;
  construct_version(contents,after,patch);
  std::swap(contents,after);
}

void cvs_repository::store_delta(const std::string &new_contents, const std::string &old_contents, const std::string &patch, const hexenc<id> &from, hexenc<id> &to)
{
  data dat(new_contents);
  calculate_ident(dat,to);
  if (!app.db.file_version_exists(to))
  { 
    base64< gzip<delta> > del;
    diff(data(old_contents), data(new_contents), del);
    app.db.put_file_version(from,to,del);
    if (file_id_ticker.get()) ++(*file_id_ticker);
  }
}

static void 
build_change_set(const cvs_client &c, const cvs_manifest &oldm, const cvs_manifest &newm,
                 change_set & cs)
{
  cs = change_set();

  L(F("build_change_set(%d,%d,)\n") % oldm.size() % newm.size());
  
  for (cvs_manifest::const_iterator f = oldm.begin(); f != oldm.end(); ++f)
    {
      cvs_manifest::const_iterator fn = newm.find(f->first);
      if (fn==newm.end())
      {  
        L(F("deleting file '%s'\n") % c.shorten_path(f->first));              
        cs.delete_file(c.shorten_path(f->first));
      }
      else 
        { if (f->second->sha1sum == fn->second->sha1sum)
            {
//              L(F("skipping preserved entry state '%s' on '%s'\n")
//                % fn->second->sha1sum % fn->first);         
            }
          else
            {
              L(F("applying state delta on '%s' : '%s' -> '%s'\n") 
                % c.shorten_path(fn->first) % f->second->sha1sum % fn->second->sha1sum);          
              I(!fn->second->sha1sum().empty());
              cs.apply_delta(c.shorten_path(fn->first), f->second->sha1sum, fn->second->sha1sum);
            }
        }  
    }
  for (cvs_manifest::const_iterator f = newm.begin(); f != newm.end(); ++f)
    {
      cvs_manifest::const_iterator fo = oldm.find(f->first);
      if (fo==oldm.end())
      {  
        L(F("adding file '%s' as '%s'\n") % f->second->sha1sum % c.shorten_path(f->first));
        I(!f->second->sha1sum().empty());
        cs.add_file(c.shorten_path(f->first), f->second->sha1sum);
      }
    }
}

void cvs_repository::check_split(const cvs_file_state &s, const cvs_file_state &end, 
          const std::set<cvs_edge>::iterator &e)
{ cvs_file_state s2=s;
  ++s2;
  if (s2==end) return;
  I(s->since_when!=s2->since_when);
  // check ins must not overlap (next revision must lie beyond edge)
  if (s2->since_when <= e->time2)
  { W(F("splitting edge %ld-%ld at %ld\n") % e->time % e->time2 % s2->since_when);
    cvs_edge new_edge=*e;
    I(s2->since_when-1>=e->time);
    e->time2=s2->since_when-1;
    new_edge.time=s2->since_when;
    edges.insert(new_edge);
  }
}

void cvs_repository::prime()
{ get_all_files();
  revision_ticker.reset(0);
  cvs_edges_ticker.reset(new ticker("edges", "E", 10));
  for (std::map<std::string,file_history>::iterator i=files.begin();i!=files.end();++i)
  { RLog(prime_log_cb(*this,i),false,"-b",i->first.c_str(),0);
  }
  // remove duplicate states
  for (std::set<cvs_edge>::iterator i=edges.begin();i!=edges.end();)
  { if (i->changelog_valid || i->author.size()) { ++i; continue; }
    std::set<cvs_edge>::iterator j=i;
    j++;
    I(j!=edges.end());
    I(j->time==i->time);
    I(i->files.empty());
//    I(i->revision.empty());
    edges.erase(i);
    if (cvs_edges_ticker.get()) --(*cvs_edges_ticker);
    i=j; 
  }
  
  // join adjacent check ins (same author, same changelog)
  for (std::set<cvs_edge>::iterator i=edges.begin();i!=edges.end();)
  { std::set<cvs_edge>::iterator j=i;
    j++; // next one
    if (j==edges.end()) break;
    
    I(j->time2==j->time); // make sure we only do this once
    I(i->time2<=j->time); // should be sorted ...
    if (!i->similar_enough(*j)) 
    { ++i; continue; }
    I((j->time-i->time2)<=time_t(cvs_edge::cvs_window)); // just to be sure
    I(i->author==j->author);
    I(i->changelog==j->changelog);
    I(i->time2<j->time); // should be non overlapping ...
    L(F("joining %ld-%ld+%ld\n") % i->time % i->time2 % j->time);
    i->time2=j->time;
    edges.erase(j);
  }
  
  // get the contents
  for (std::map<std::string,file_history>::iterator i=files.begin();i!=files.end();++i)
  { vector<piece> file_contents;
    std::string keyword_substitution;
    I(!i->second.known_states.empty());
    { std::set<file_state>::iterator s2=i->second.known_states.begin();
      std::string revision=s2->cvs_version;
      cvs_client::checkout c=CheckOut(i->first,revision);
//    I(c.mod_time==?);
      const_cast<bool&>(s2->dead)=c.dead;
      if (!c.dead)
      { store_contents(c.contents, const_cast<hexenc<id>&>(s2->sha1sum));
        const_cast<unsigned&>(s2->size)=c.contents.size();
        index_deltatext(c.contents,file_contents);
        keyword_substitution=c.keyword_substitution;
      }
    }
    for (std::set<file_state>::iterator s=i->second.known_states.begin();
          s!=i->second.known_states.end();++s)
    { std::set<file_state>::iterator s2=s;
      ++s2;
      if (s2==i->second.known_states.end()) break;
      // s2 gets changed
      cvs_revision_nr srev(s->cvs_version);
      I(srev.is_parent_of(s2->cvs_version));
      if (s->dead)
      { cvs_client::checkout c=CheckOut(i->first,s2->cvs_version);
        I(!c.dead); // dead->dead is no change, so shouldn't get a number
        I(!s2->dead);
        store_contents(c.contents, const_cast<hexenc<id>&>(s2->sha1sum));
        const_cast<unsigned&>(s2->size)=c.contents.size();
        index_deltatext(c.contents,file_contents);
      }
      else if (s2->dead) // short circuit if we already know it's dead
      { L(F("file %s: revision %s already known to be dead\n") % i->first % s2->cvs_version);
      }
      else
      { cvs_client::update u=Update(i->first,s->cvs_version,s2->cvs_version,keyword_substitution);
        if (u.removed)
        { const_cast<bool&>(s2->dead)=true;
        }
        else if (!u.checksum.empty())
        { // const_cast<std::string&>(s2->rcs_patch)=u.patch;
          const_cast<std::string&>(s2->md5sum)=u.checksum;
          const_cast<unsigned&>(s2->patchsize)=u.patch.size();
          std::string old_contents;
          build_string(file_contents, old_contents);
          apply_delta(file_contents, u.patch);
          std::string contents;
          build_string(file_contents, contents);
          // check md5
          CryptoPP::MD5 hash;
          std::string md5sum=xform<CryptoPP::HexDecoder>(u.checksum);
          I(md5sum.size()==CryptoPP::MD5::DIGESTSIZE);
          if (hash.VerifyDigest(reinterpret_cast<byte const *>(md5sum.c_str()),
              reinterpret_cast<byte const *>(contents.c_str()),
              contents.size()))
          { store_delta(contents, old_contents, u.patch, s->sha1sum, const_cast<hexenc<id>&>(s2->sha1sum));
          }
          else
          { throw oops("MD5 sum wrong");
          }
        }
        else
        {
          store_contents(u.contents, const_cast<hexenc<id>&>(s2->sha1sum));
          const_cast<unsigned&>(s2->size)=u.contents.size();
          index_deltatext(u.contents,file_contents);
        }
      }
    }
  }

  // fill in file states at given point
  cvs_manifest current_manifest;
  for (std::set<cvs_edge>::iterator e=edges.begin();e!=edges.end();++e)
  { for (std::map<std::string,file_history>::const_iterator f=files.begin();f!=files.end();++f)
    { I(!f->second.known_states.empty());
      if (f->second.known_states.begin()->since_when > e->time2)
        continue; // the file does not exist yet
      cvs_manifest::iterator mi=current_manifest.find(f->first);
      if (mi==current_manifest.end()) // the file is currently dead
      { cvs_file_state s=f->second.known_states.begin();
        // find next revision _within_ edge timespan
        for (;s!=f->second.known_states.end();)
        { cvs_file_state s2=s;
          ++s2;
          if (s2==f->second.known_states.end() || s2->since_when > e->time2)
            break;
          s=s2;
        }
        I(s!=f->second.known_states.end());
        // a live revision was found
        if (s->since_when <= e->time2 && !s->dead)
        { current_manifest[f->first]=s;
          I(!s->sha1sum().empty());
          check_split(s,f->second.known_states.end(),e);
        }
      }
      else // file was present in last manifest, check next revision
      { cvs_file_state s=mi->second;
        ++s;
        if (s!=f->second.known_states.end() && s->since_when <= e->time2)
        { if (s->dead) current_manifest.erase(mi);
          else 
          { mi->second=s;
            I(!s->sha1sum().empty());
          }
          check_split(s,f->second.known_states.end(),e);
        }
      }
    }
    e->files=current_manifest;
  }
  // commit them all
  
  cvs_edges_ticker.reset(0);
  revision_ticker.reset(new ticker("revisions", "R", 3));
  cvs_manifest empty;
  revision_id parent_rid;
  manifest_id parent_mid;
  manifest_map parent_map;
  manifest_map child_map=parent_map;
  packet_db_writer dbw(app);
  
  const cvs_manifest *oldmanifestp=&empty;
  for (std::set<cvs_edge>::iterator e=edges.begin(); e!=edges.end(); ++e)
  { change_set cs;
    build_change_set(*this,*oldmanifestp,e->files,cs);
    if (*oldmanifestp==e->files) 
    { W(F("null edge (no changed files) @%ld skipped\n") % e->time);
      continue;
    }
    I(!(*oldmanifestp==e->files));
    apply_change_set(cs, child_map);
    if (child_map.empty()) 
    { W(F("empty edge (no files in manifest) @%ld skipped\n") % e->time);
      // perhaps begin a new tree:
      // parent_rid=revision_id();
      // parent_mid=manifest_id();
      continue;
    }
    manifest_id child_mid;
    calculate_ident(child_map, child_mid);

    revision_set rev;
    rev.new_manifest = child_mid;
    rev.edges.insert(make_pair(parent_rid, make_pair(parent_mid, cs)));
    revision_id child_rid;
    calculate_ident(rev, child_rid);
    L(F("CVS Sync: Inserting revision %s (%s) into repository\n") % child_rid % child_mid);

    if (app.db.manifest_version_exists(child_mid))
    {
        L(F("existing path to %s found, skipping\n") % child_mid);
    }
    else if (parent_mid.inner()().empty())
    {
      manifest_data mdat;
      I(!child_map.empty());
      write_manifest_map(child_map, mdat);
      app.db.put_manifest(child_mid, mdat);
    }
    else
    { 
      base64< gzip<delta> > del;
      I(!child_map.empty());
      diff(parent_map, child_map, del);
      app.db.put_manifest_version(parent_mid, child_mid, del);
    }
    e->revision=child_rid.inner();
    if (! app.db.revision_exists(child_rid))
    { app.db.put_revision(child_rid, rev);
      if (revision_ticker.get()) ++(*revision_ticker);
    }
    cert_revision_in_branch(child_rid, app.branch_name(), app, dbw); 
    cert_revision_author(child_rid, e->author+"@"+host, app, dbw); 
    cert_revision_changelog(child_rid, e->changelog, app, dbw);
    cert_revision_date_time(child_rid, e->time, app, dbw);
    cert_cvs(*e, dbw);

    // now apply same change set to parent_map, making parent_map == child_map
    apply_change_set(cs, parent_map);
    parent_mid = child_mid;
    parent_rid = child_rid;
    oldmanifestp=&e->files;
  }
}

void cvs_repository::cert_cvs(const cvs_edge &e, packet_consumer & pc)
{ std::string content=host+":"+root+"/"+module+"\n";
  for (cvs_manifest::const_iterator i=e.files.begin(); i!=e.files.end(); ++i)
  { content+=i->second->cvs_version+" "+shorten_path(i->first)+"\n";
  }
  cert t;
  make_simple_cert(e.revision, cert_name(cvs_cert_name), content, app, t);
  revision<cert> cc(t);
  pc.consume_revision_cert(cc);
//  put_simple_revision_cert(e.revision, "cvs_revisions", content, app, pc);
}

cvs_repository::cvs_repository(app_state &_app, const std::string &repository, const std::string &module)
      : cvs_client(repository,module), app(_app), file_id_ticker(), 
        revision_ticker(), cvs_edges_ticker()
{
  file_id_ticker.reset(new ticker("file ids", "F", 10));
  revision_ticker.reset(new ticker("revisions", "R", 3));
}

void cvs_sync::sync(const std::string &repository, const std::string &module,
            app_state &app)
{
  {
    // early short-circuit to avoid failure after lots of work
    rsa_keypair_id key;
    N(guess_default_key(key,app),
      F("no unique private key for cert construction"));
    N(priv_key_exists(app, key),
      F("no private key '%s' found in database or get_priv_key hook") % key);
    // Require the password early on, so that we don't do lots of work
    // and then die.
    N(app.db.public_key_exists(key),
      F("no public key '%s' found in database") % key);
    base64<rsa_pub_key> pub;
    app.db.get_key(key, pub);
    base64< arc4<rsa_priv_key> > priv;
    load_priv_key(app, key, priv);
    require_password(app.lua, key, pub, priv);
  }
  
  cvs_sync::cvs_repository repo(app,repository,module);
// DEBUGGING
  repo.GzipStream(3);
  transaction_guard guard(app.db);

  { std::vector< revision<cert> > certs;
    app.db.get_revision_certs(cvs_cert_name, certs);
    // erase_bogus_certs ?
    repo.process_certs(certs);
  }
  
  // initial checkout
  if (repo.empty()) 
    repo.prime();
  else repo.update();
  
  guard.commit();      
}

cvs_file_state cvs_repository::remember(std::set<file_state> &s,const file_state &fs)
{ for (std::set<file_state>::iterator i=s.begin();i!=s.end();++i)
  { if (i->cvs_version==fs.cvs_version)
    { if (i->since_when>fs.since_when) 
        const_cast<time_t&>(i->since_when)=fs.since_when;
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

  std::string needed_cert=host+":"+root+"/"+module+"\n";
  for (vector<revision<cert> >::const_iterator i=certs.begin(); i!=certs.end(); ++i)
  { // populate data structure using these certs
    cert_value cvs_revisions;
    decode_base64(i->inner().value, cvs_revisions);
    if (cvs_revisions().size()>needed_cert.size() 
      && cvs_revisions().substr(0,needed_cert.size())==needed_cert)
    { // parse and add the cert
      ++(*cert_ticker);
      cvs_edge e;
      e.revision=i->inner().ident;
      // get author + date 
      std::vector< revision<cert> > edge_certs;
      app.db.get_revision_certs(i->inner().ident,edge_certs);
      // erase_bogus_certs ?
      for (std::vector< revision<cert> >::const_iterator c=edge_certs.begin();
                c!=edge_certs.end();++c)
      { cert_value value;
        decode_base64(c->inner().value, value);   
        if (c->inner().name()==date_cert_name)
        { L(F("date cert %s\n")%value());
          std::string posix_format=value();
          std::string::size_type next_illegal=0;
          while ((next_illegal=posix_format.find_first_of("-:"))!=std::string::npos)
            posix_format.erase(next_illegal,1);
          boost::posix_time::ptime tmp= boost::posix_time::from_iso_string(posix_format);
          boost::posix_time::time_duration dur= tmp
              -boost::posix_time::ptime(boost::gregorian::date(1970,1,1),
                              boost::posix_time::time_duration(0,0,0,0));
          e.time=e.time2=dur.total_seconds();
        }
        else if (c->inner().name()==author_cert_name)
        { e.author=value();
        }
        else if (c->inner().name()==changelog_cert_name)
        { e.changelog=value();
          e.changelog_valid=true;
        }
      }
      
      std::vector<piece> pieces;
      index_deltatext(cvs_revisions(),pieces);
      I(!pieces.empty());
      manifest_id mid;
      app.db.get_revision_manifest(i->inner().ident,mid);
      manifest_map manifest;
      app.db.get_manifest(mid,manifest);
//      manifest;
      for (std::vector<piece>::const_iterator p=pieces.begin()+1;p!=pieces.end();++p)
      { std::string line=**p;
        I(!line.empty());
        I(line[line.size()-1]=='\n');
        line.erase(line.size()-1,1);
        std::string::size_type space=line.find(' ');
        I(space!=std::string::npos);
        std::string monotone_path=line.substr(space+1);
        std::string path=module+"/"+monotone_path;

        file_state fs;
        fs.since_when=e.time;
        fs.cvs_version=line.substr(0,space);
        manifest_map::const_iterator iter_file_id=manifest.find(monotone_path);
        I(iter_file_id!=manifest.end());
        fs.sha1sum=iter_file_id->second.inner();
        fs.log_msg=e.changelog;
        cvs_file_state cfs=remember(files[path].known_states,fs);
        e.files.insert(std::make_pair(path,cfs));
#if 0        
        // this is grossly inefficient because I store a file_state per
        // monotone revision instead of per rcs/file revision
        std::pair<cvs_file_state,bool> res=files[path].known_states.insert(fs);
        I(res.second);
        e.files.insert(std::make_pair(path,res.first));
#endif        
      }
      edges.insert(e);
    }
  }
//  compact_files();
  debug();
}

struct cvs_repository::update_cb : cvs_client::update_callbacks
{ cvs_repository &repo;
  update_cb(cvs_repository &r) : repo(r) {}
  virtual void operator()(const cvs_client::update &u) const
  { std::cerr << "file " << u.file << ": " << u.new_revision << ' ' 
        << u.contents.size() << ' ' << u.patch.size() 
        << (u.removed ? " dead" : "") << '\n';
  }
};

void cvs_repository::update()
{ I(!edges.empty());
  const cvs_edge &now=*(edges.rbegin());
  I(!now.revision().empty());
  std::vector<update_args> file_revisions;
  file_revisions.reserve(now.files.size());
  for (cvs_manifest::const_iterator i=now.files.begin();i!=now.files.end();++i)
    file_revisions.push_back(update_args(i->first,i->second->cvs_version));
  Update(file_revisions,update_cb(*this));
}
