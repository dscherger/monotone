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

bool cvs_revision_nr::operator==(const cvs_revision_nr &b) const
{ return parts==b.parts;
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

// cvs_repository ----------------------

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
      else if (!j->sha1sum().empty()) std::cerr << j->sha1sum().substr(0,3) << j->keyword_substitution;
      ++j;
      if (j!=i->second.known_states.end()) std::cerr << ',';
    }
    std::cerr << ")\n";
  }
  std::cerr << "Tags :\n";
  for (std::map<std::string,std::map<std::string,std::string> >::const_iterator i=tags.begin();
      i!=tags.end();++i)
  { std::cerr << i->first << "(" << i->second.size() << " files)\n";
  }
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
{ L(F("prime_log_cb %s:%s %d %s %d %s\n") % file % revision % checkin_time
        % author % message.size() % dead);
  I(i->first==file);
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
  { 
    base64< gzip<delta> > del;
    diff(data(old_contents), data(new_contents), del);
    base64< gzip<data> > packed;
    pack(data(new_contents), packed);
    if (packed().size()<=del().size())
    // the data is smaller or of equal size to the patch
      app.db.put_file(to, packed);
    else 
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

void cvs_repository::join_edge_parts(std::set<cvs_edge>::iterator i)
{ for (;i!=edges.end();)
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
    if (u.mod_time!=s2->since_when)
    { W(F("update time %ld and log time %ld disagree\n") % u.mod_time % s2->since_when);
    }
    std::string old_contents=contents;
    { std::vector<piece> file_contents;
      index_deltatext(contents,file_contents);
      apply_delta(file_contents, u.patch);
      build_string(file_contents, contents);
    }
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
  I(srev.is_parent_of(s2->cvs_version));
  if (s->dead)
  { cvs_client::checkout c=CheckOut(file,s2->cvs_version);
    I(!c.dead); // dead->dead is no change, so shouldn't get a number
    I(!s2->dead);
    // I(s2->since_when==c.mod_time);
    if (c.mod_time!=s2->since_when)
    { W(F("checkout time %ld and log time %ld disagree\n") % c.mod_time % s2->since_when);
    }
    store_contents(c.contents, const_cast<hexenc<id>&>(s2->sha1sum));
    const_cast<unsigned&>(s2->size)=c.contents.size();
    contents=c.contents;
    const_cast<std::string&>(s2->keyword_substitution)=c.keyword_substitution;
  }
  else if (s2->dead) // short circuit if we already know it's dead
  { L(F("file %s: revision %s already known to be dead\n") % file % s2->cvs_version);
  }
  else
  { cvs_client::update u=Update(file,s->cvs_version,s2->cvs_version,s->keyword_substitution);
    store_update(s,s2,u,contents);
  }
}

void cvs_repository::store_checkout(std::set<file_state>::iterator s2,
        const cvs_client::checkout &c, std::string &file_contents)
{ const_cast<bool&>(s2->dead)=c.dead;
  if (!c.dead)
  { // I(c.mod_time==s2->since_when);
    if (c.mod_time!=s2->since_when)
    { W(F("checkout time %ld and log time %ld disagree\n") % c.mod_time % s2->since_when);
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
    current_manifest=before->files;
  }
  for (;e!=edges.end();++e)
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
}

void cvs_repository::commit_revisions(std::set<cvs_edge>::iterator e)
{ cvs_manifest empty;
  revision_id parent_rid;
  manifest_id parent_mid;
  manifest_map parent_map;
  manifest_map child_map=parent_map;
  packet_db_writer dbw(app);
  
  const cvs_manifest *oldmanifestp=&empty;
  if (e!=edges.begin())
  { std::set<cvs_edge>::const_iterator before=e;
    --before;
    I(!before->revision().empty());
    parent_rid=before->revision;
    app.db.get_revision_manifest(parent_rid,parent_mid);
    app.db.get_manifest(parent_mid,parent_map);
    child_map=parent_map;
    oldmanifestp=&before->files;
  }
  for (; e!=edges.end(); ++e)
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

void cvs_repository::prime()
{ get_all_files();
  revision_ticker.reset(0);
  cvs_edges_ticker.reset(new ticker("edges", "E", 10));
  for (std::map<std::string,file_history>::iterator i=files.begin();i!=files.end();++i)
  { RLog(prime_log_cb(*this,i),false,"-b",i->first.c_str(),0);
  }
  // remove duplicate states (because some edges were added by the 
  // get_all_files method
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
  join_edge_parts(edges.begin());
  
  // get the contents
  for (std::map<std::string,file_history>::iterator i=files.begin();i!=files.end();++i)
  { std::string file_contents;
    I(!i->second.known_states.empty());
    { std::set<file_state>::iterator s2=i->second.known_states.begin();
      cvs_client::checkout c=CheckOut(i->first,s2->cvs_version);
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

  // fill in file states at given point
  fill_manifests(edges.begin());
  // commit them all
  
  cvs_edges_ticker.reset(0);
  revision_ticker.reset(new ticker("revisions", "R", 3));

  commit_revisions(edges.begin());
}

void cvs_repository::cert_cvs(const cvs_edge &e, packet_consumer & pc)
{ std::string content=host+":"+root+"/"+module+"\n";
  for (cvs_manifest::const_iterator i=e.files.begin(); i!=e.files.end(); ++i)
  { content+=i->second->cvs_version;
    if (!i->second->keyword_substitution.empty())
      content+="/"+i->second->keyword_substitution;
    content+=" "+shorten_path(i->first)+"\n";
  }
  cert t;
  make_simple_cert(e.revision, cert_name(cvs_cert_name), content, app, t);
  revision<cert> cc(t);
  pc.consume_revision_cert(cc);
}

cvs_repository::cvs_repository(app_state &_app, const std::string &repository, const std::string &module)
      : cvs_client(repository,module), app(_app), file_id_ticker(), 
        revision_ticker(), cvs_edges_ticker()
{
  file_id_ticker.reset(new ticker("file ids", "F", 10));
  revision_ticker.reset(new ticker("revisions", "R", 3));
}

static void test_key_availability(app_state &app)
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

std::set<cvs_edge>::iterator cvs_repository::last_known_revision()
{ I(!edges.empty());
  std::set<cvs_edge>::iterator now_iter=edges.end();
  --now_iter;
  return now_iter;
}

cvs_edge::cvs_edge(const revision_id &rid, app_state &app)
 : changelog_valid(), time(), time2()
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
    { L(F("date cert %s\n")%value());
      std::string posix_format=value();
      std::string::size_type next_illegal=0;
      while ((next_illegal=posix_format.find_first_of("-:"))!=std::string::npos)
        posix_format.erase(next_illegal,1);
      boost::posix_time::ptime tmp= boost::posix_time::from_iso_string(posix_format);
      boost::posix_time::time_duration dur= tmp
          -boost::posix_time::ptime(boost::gregorian::date(1970,1,1),
                          boost::posix_time::time_duration(0,0,0,0));
      time=time2=dur.total_seconds();
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

std::set<cvs_edge>::iterator cvs_repository::commit(
      std::set<cvs_edge>::iterator parent, const revision_id &rid)
{ // check that it's the last one
  L(F("commit %s -> %s\n") % parent->revision % rid);
  { std::set<cvs_edge>::iterator test=parent;
    ++test;
    I(test==edges.end());
  }
  // a bit like process_certs
  cvs_edge e(rid,app);

  revision_set rs;
  app.db.get_revision(rid, rs);
  std::vector<commit_arg> commits;
  
  for (edge_map::const_iterator j = rs.edges.begin(); 
       j != rs.edges.end();
       ++j)
  { if (!(edge_old_revision(j) == parent->revision)) 
    { L(F("%s != %s\n") % edge_old_revision(j) % parent->revision);
      continue;
    }
    const change_set &cs=edge_changes(j);
    N(cs.rearrangement.renamed_dirs.empty(),
        F("I can't commit directory renames yet\n"));
    N(cs.rearrangement.deleted_dirs.empty(),
        F("I can't commit directory deletions yet\n"));

    for (std::set<file_path>::const_iterator i=cs.rearrangement.deleted_files.begin();
            i!=cs.rearrangement.deleted_files.end(); ++i)
    { commit_arg a;
      a.file=module+"/"+(*i)();
      cvs_manifest::const_iterator old=parent->files.find((*i)());
      I(old!=parent->files.end());
      a.removed=true;
      a.old_revision=old->second->cvs_version;
      a.keyword_substitution=old->second->keyword_substitution;
      commits.push_back(a);
      L(F("delete %s -%s %s\n") % a.file % a.old_revision % a.keyword_substitution);
    }

    for (std::map<file_path,file_path>::const_iterator i
                            =cs.rearrangement.renamed_files.begin();
            i!=cs.rearrangement.renamed_files.end(); ++i)
    { commit_arg a; // remove
      a.file=module+"/"+i->first();
      cvs_manifest::const_iterator old=parent->files.find(i->first());
      I(old!=parent->files.end());
      a.removed=true;
      a.old_revision=old->second->cvs_version;
      a.keyword_substitution=old->second->keyword_substitution;
      commits.push_back(a);
      L(F("rename from %s -%s %s\n") % a.file % a.old_revision % a.keyword_substitution);
      
      a=commit_arg(); // add
      a.file=module+"/"+i->second();
      I(!old->second->sha1sum().empty());
      file_data dat;
      app.db.get_file_version(old->second->sha1sum,dat);
      data unpacked;
      unpack(dat.inner(), unpacked);
      a.new_content=unpacked();
      commits.push_back(a);
      L(F("rename to %s %d\n") % a.file % a.new_content.size());
    }
    
    // added files also have a delta, so we can ignore this list

    for (change_set::delta_map::const_iterator i=cs.deltas.begin();
            i!=cs.deltas.end(); ++i)
    { 
      commit_arg a;
      a.file=module+"/"+i->first();
      cvs_manifest::const_iterator old=parent->files.find(i->first());
      if (old!=parent->files.end())
      { a.old_revision=old->second->cvs_version;
        a.keyword_substitution=old->second->keyword_substitution;
      }
      file_data dat;
      app.db.get_file_version(i->second.second,dat);
      data unpacked;
      unpack(dat.inner(), unpacked);
      a.new_content=unpacked();
      commits.push_back(a);
      L(F("delta %s %s %s %d\n") % a.file % a.old_revision % a.keyword_substitution
          % a.new_content.size());
    }

    I(!commits.empty());
    std::map<std::string,std::pair<std::string,std::string> > result
      =Commit(e.changelog,e.time,commits);
    if (result.empty()) return edges.end();
    e.files=parent->files;
    
    for (std::map<std::string,std::pair<std::string,std::string> >::const_iterator
            i=result.begin(); i!=result.end(); ++i)
    { cvs_manifest::iterator manifestiter=e.files.find(shorten_path(i->first));
      if (i->second.first.empty())
      { I(manifestiter!=e.files.end());
        e.files.erase(manifestiter);
      }
      else
      { file_state fs(e.time,i->second.first);
        fs.log_msg=e.changelog;
        fs.keyword_substitution=i->second.second;
        change_set::delta_map::const_iterator mydelta=cs.deltas.find(shorten_path(i->first));
        I(mydelta!=cs.deltas.end());
        fs.sha1sum=mydelta->second.second.inner();
        std::pair<std::set<file_state>::iterator,bool> newelem=
            files[i->first].known_states.insert(fs);
        I(newelem.second);
        manifestiter->second=newelem.first;
      }
    }
    packet_db_writer dbw(app);
    cert_cvs(e, dbw);
    edges.insert(e);
    return --(edges.end());
  }
  W(F("no matching parent found\n"));
  return edges.end();
}

void cvs_repository::commit()
{
  std::set<cvs_edge>::iterator now_iter=last_known_revision();
  while (now_iter!=edges.end())
  { const cvs_edge &now=*now_iter;
    I(!now.revision().empty());
    
    L(F("looking for children of revision %s\n") % now.revision);
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
    if (children.size()>1)
    { W(F("several children found for %s:\n") % now.revision);
      for (std::set<revision_id>::const_iterator i=children.begin();
                    i!=children.end();++i)
      { W(F("%s\n") % *i);
      }
      return;
    }
    now_iter=commit(now_iter,*children.begin());
  }
}

void cvs_sync::push(const std::string &repository, const std::string &module,
            app_state &app)
{ test_key_availability(app);
  cvs_sync::cvs_repository repo(app,repository,module);
// turned off for DEBUGGING
  if (!getenv("CVS_CLIENT_LOG"))
    repo.GzipStream(3);
  transaction_guard guard(app.db);

  { std::vector< revision<cert> > certs;
    app.db.get_revision_certs(cvs_cert_name, certs);
    // erase_bogus_certs ?
    repo.process_certs(certs);
  }
  
  N(!repo.empty(),
    F("no revision certs for this repository/module\n"));

  repo.commit();
  
  guard.commit();      
}

void cvs_sync::pull(const std::string &repository, const std::string &module,
            app_state &app)
{ test_key_availability(app);
  
  cvs_sync::cvs_repository repo(app,repository,module);
// turned off for DEBUGGING
  if (!getenv("CVS_CLIENT_LOG"))
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
      cvs_edge e(i->inner().ident,app);

      std::vector<piece> pieces;
      // in Zeilen aufteilen
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
        // the format is "<revsion>[/<keyword_substitution>] <path>\n"
        // e.g. "1.1 .cvsignore",     "1.43/-kb test.png"
        std::string::size_type space=line.find(' ');
        I(space!=std::string::npos);
        std::string monotone_path=line.substr(space+1);
        std::string path=module+"/"+monotone_path;
        // look for the optional initial slash separating the keyword mode
        std::string::size_type slash=line.find('/');
        if (slash==std::string::npos || slash>space)
          slash=space;

        file_state fs;
        fs.since_when=e.time;
        fs.cvs_version=line.substr(0,slash);
        if (space!=slash) 
          fs.keyword_substitution=line.substr(slash+1,space-(slash+1));
        manifest_map::const_iterator iter_file_id=manifest.find(monotone_path);
        I(iter_file_id!=manifest.end());
        fs.sha1sum=iter_file_id->second.inner();
        fs.log_msg=e.changelog;
        cvs_file_state cfs=remember(files[path].known_states,fs);
        e.files.insert(std::make_pair(path,cfs));
      }
      edges.insert(e);
    }
  }
  debug();
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
{ std::set<cvs_edge>::iterator now_iter=last_known_revision();
  const cvs_edge &now=*now_iter;
  I(!now.revision().empty());
  std::vector<update_args> file_revisions;
  std::vector<cvs_client::update> results;
  file_revisions.reserve(now.files.size());
  for (cvs_manifest::const_iterator i=now.files.begin();i!=now.files.end();++i)
    file_revisions.push_back(update_args(i->first,i->second->cvs_version,
                            std::string(),i->second->keyword_substitution));
  Update(file_revisions,update_cb(*this,results));
  for (std::vector<cvs_client::update>::const_iterator i=results.begin();i!=results.end();++i)
  { // 2do: use tags
    cvs_manifest::const_iterator now_file=now.files.find(i->file);
    std::string last_known_revision;
    std::map<std::string,file_history>::iterator f=files.find(i->file);
    
    if (now_file!=now.files.end())
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
      RLog(prime_log_cb(*this,f),false,"-b","-N",
        "--",i->file.c_str(),0);
    else
      // -b causes -r to get ignored on 0.12
      RLog(prime_log_cb(*this,f),false,/*"-b",*/"-N",
        ("-r"+last_known_revision+"::").c_str(),"--",i->file.c_str(),0);
    
    std::string file_contents,initial_contents;
    if(last==f->second.known_states.end())
    { last=f->second.known_states.begin();
      I(last!=f->second.known_states.end());
      std::set<file_state>::iterator s2=last;
      cvs_client::checkout c=CheckOut(i->file,s2->cvs_version);
      store_checkout(s2,c,file_contents);
    }
    else
    { I(!last->sha1sum().empty());
      file_data dat;
      app.db.get_file_version(last->sha1sum,dat);
      data unpacked;
      unpack(dat.inner(), unpacked);
      file_contents=unpacked();
      initial_contents=file_contents;
    }
    for (std::set<file_state>::const_iterator s=last;
                  s!=f->second.known_states.end();++s)
    { std::set<file_state>::const_iterator s2=s;
      ++s2;
      if (s2==f->second.known_states.end()) break;
      if (s2->cvs_version==i->new_revision)
      { // we do not need to ask the host, we already did ...
        store_update(last,s2,*i,initial_contents);
        break;
      }
      else
      { update(s,s2,i->file,file_contents);
      }
    }
  }
  std::set<cvs_edge>::iterator dummy_iter=now_iter;
  ++dummy_iter;
  join_edge_parts(dummy_iter);
  
  fill_manifests(dummy_iter);
  debug();
  commit_revisions(dummy_iter);
}
