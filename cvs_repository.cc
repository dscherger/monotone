// copyright (C) 2005 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "cvs_sync.hh"
#include "keys.hh"
#include "transforms.hh"

using namespace cvs_sync;

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

cvs_revision::cvs_revision(const std::string &x)
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

void cvs_revision::operator++(int)
{ if (parts.empty()) return;
  parts.back()++;
}

std::string cvs_revision::get_string() const
{ std::string result;
  for (std::vector<int>::const_iterator i=parts.begin();i!=parts.end();)
  { result+= (F("%d") % *i).str();
    ++i;
    if (i!=parts.end()) result+",";
  }
  return result;
}

bool cvs_revision::is_parent_of(const cvs_revision &child) const
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
bool cvs_revision::is_branch() const 
{ return parts.size()&1;
}

// cvs_repository ----------------------

void cvs_repository::ticker() const
{ cvs_client::ticker(false);
  if (files_inserted) std::cerr << "[file ids added: " << files_inserted;
  else std::cerr << " [files: " << files.size();
  std::cerr << "] [edges: " << edges.size() 
          << "] [tags: "  << tags.size() 
          << "]\n";
}

struct cvs_repository::now_log_cb : rlog_callbacks
{ cvs_repository &repo;
  now_log_cb(cvs_repository &r) : repo(r) {}
  virtual void file(const std::string &file,const std::string &head_rev) const
  { repo.files[file]; }
  virtual void tag(const std::string &file,const std::string &tag, 
        const std::string &revision) const {}
  virtual void revision(const std::string &file,time_t t,
        const std::string &rev,const std::string &author,
        const std::string &state,const std::string &log) const {}
};

struct cvs_repository::now_list_cb : rlist_callbacks
{ cvs_repository &repo;
  now_list_cb(cvs_repository &r) : repo(r) {}
  virtual void file(const std::string &name, time_t last_change,
        const std::string &last_rev, bool dead) const
  { repo.files[name].known_states.insert(file_state(last_change,last_rev,dead));
    repo.edges.insert(cvs_edge(last_change));
  }
};

const cvs_repository::tree_state_t &cvs_repository::now()
{ if (edges.empty())
  { if (CommandValid("rlist"))
    { RList(now_list_cb(*this),false,"-l","-R","-d","--",module.c_str(),0);
    }
    else // less efficient? ...
    { I(CommandValid("rlog"));
      RLog(now_log_cb(*this),false,"-N","-h","--",module.c_str(),0);
    }
    ticker();
    // prime
//    prime();
  }
  static cvs_repository::tree_state_t dummy_result;
  return dummy_result; // wrong of course
}

void cvs_repository::debug() const
{ // edges set<cvs_edge>
  std::cerr << "Edges :\n";
  for (std::set<cvs_edge>::const_iterator i=edges.begin();
      i!=edges.end();++i)
  { std::cerr << "[" << i->time;
    if (i->time!=i->time2) std::cerr << '+' << (i->time2-i->time);
    std::cerr << ',' << i->author << ',' 
      << i->changelog.size() << "]\n";
  }
//  std::cerr << '\n';
  // files map<string,file>
  std::cerr << "Files :\n";
  for (std::map<std::string,file>::const_iterator i=files.begin();
      i!=files.end();++i)
  { unsigned len=0;
    if (cvs_client::begins_with(i->first,module,len))
    { if (i->first[len]=='/') ++len;
      std::cerr << i->first.substr(len);
    }
    else std::cerr << i->first;
    std::cerr << "(";
    for (std::set<file_state>::const_iterator j=i->second.known_states.begin();
          j!=i->second.known_states.end();)
    { if (j->dead) std::cerr << "dead";
      else if (j->size) std::cerr << j->size;
      else if (j->patchsize) std::cerr << 'p' << j->patchsize;
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
  std::map<std::string,struct cvs_sync::file>::iterator i;
  prime_log_cb(cvs_repository &r,const std::map<std::string,struct cvs_sync::file>::iterator &_i) 
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
  repo.edges.insert(cvs_edge(message,checkin_time,author));
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

void cvs_repository::store_contents(app_state &app, const std::string &contents, hexenc<id> &sha1sum)
{
  data dat(contents);
  calculate_ident(dat,sha1sum);
  if (!app.db.file_version_exists(sha1sum))
  { base64<gzip<data> > packed;
    pack(dat, packed);
    file_data fdat=packed;
    app.db.put_file(sha1sum, fdat);
    ++files_inserted;
  }
}

void cvs_repository::apply_delta(std::string &contents, const std::string &patch)
{
}

// a hackish way to reuse code ...
extern void rcs_put_raw_file_edge(hexenc<id> const & old_id,
                      hexenc<id> const & new_id,
                      base64< gzip<delta> > const & del,
                      database & db);

void cvs_repository::store_delta(app_state &app, const std::string &new_contents, const std::string &patch, const hexenc<id> &from, hexenc<id> &to)
{
  data dat(new_contents);
  calculate_ident(dat,to);
  if (!app.db.file_version_exists(to))
  { 
    base64<gzip<delta> > packed;
    pack(delta(patch), packed);
    // app.db.put_delta(from, to, packed, "file_deltas");
    rcs_put_raw_file_edge(from,to,packed,app.db);
    ++files_inserted;
  }
}

void cvs_repository::prime(app_state &app)
{ for (std::map<std::string,file>::iterator i=files.begin();i!=files.end();++i)
  { RLog(prime_log_cb(*this,i),false,"-b",i->first.c_str(),0);
    ticker();
  }
  // remove duplicate states
  for (std::set<cvs_edge>::iterator i=edges.begin();i!=edges.end();)
  { if (i->changelog_valid || i->author.size()) { ++i; continue; }
    std::set<cvs_edge>::iterator j=i;
    j++;
    I(j!=edges.end());
    I(j->time==i->time);
    I(i->files.empty());
    I(i->revision.empty());
    edges.erase(i);
    i=j; 
  }
  ticker();
  
  // join adjacent check ins (same author, same changelog)
  for (std::set<cvs_edge>::iterator i=edges.begin();i!=edges.end();)
  { std::set<cvs_edge>::iterator j=i;
    j++; // next one
    if (j==edges.end()) break;
    
    I(j->time2==j->time); // make sure we only do this once
    I(i->time2<=j->time); // should be sorted ...
    if (!i->similar_enough(*j)) 
    { ++i; continue; }
    I((j->time-i->time2)<=cvs_edge::cvs_window); // just to be sure
    I(i->author==j->author);
    I(i->changelog==j->changelog);
    I(i->time2<j->time); // should be non overlapping ...
    L(F("joining %ld-%ld+%ld\n") % i->time % i->time2 % j->time);
    const_cast<time_t&>(i->time2)=j->time;
    edges.erase(j);
  }
  
  // get the contents
  for (std::map<std::string,file>::iterator i=files.begin();i!=files.end();++i)
  { std::string file_contents;
    I(!i->second.known_states.empty());
    { std::set<file_state>::iterator s2=i->second.known_states.begin();
      std::string revision=s2->cvs_version;
      cvs_client::checkout c=CheckOut(i->first,revision);
//    I(c.mod_time==?);
      const_cast<bool&>(s2->dead)=c.dead;
      if (!c.dead)
      { store_contents(app, c.contents, const_cast<hexenc<id>&>(s2->sha1sum));
        const_cast<unsigned&>(s2->size)=c.contents.size();
        file_contents=c.contents;
      }
    }
    for (std::set<file_state>::iterator s=i->second.known_states.begin();
          s!=i->second.known_states.end();++s)
    { std::set<file_state>::iterator s2=s;
      ++s2;
      if (s2==i->second.known_states.end()) break;
      // s2 gets changed
      cvs_revision srev(s->cvs_version);
      I(srev.is_parent_of(s2->cvs_version));
      if (s->dead)
      { cvs_client::checkout c=CheckOut(i->first,s2->cvs_version);
        I(!c.dead); // dead->dead is no change, so shouldn't get a number
        I(!s2->dead);
        store_contents(app, c.contents, const_cast<hexenc<id>&>(s2->sha1sum));
        const_cast<unsigned&>(s2->size)=c.contents.size();
        file_contents=c.contents;
      }
      else
      { cvs_client::update u=Update(i->first,s->cvs_version,s2->cvs_version);
        if (u.removed)
        { const_cast<bool&>(s2->dead)=true;
        }
        else if (!u.checksum.empty())
        { // const_cast<std::string&>(s2->rcs_patch)=u.patch;
          const_cast<std::string&>(s2->md5sum)=u.checksum;
          const_cast<unsigned&>(s2->patchsize)=u.patch.size();
          apply_delta(file_contents, u.patch);
          // check md5
          store_delta(app, file_contents, u.patch, s->sha1sum, const_cast<hexenc<id>&>(s2->sha1sum));
        }
        else
        {
          store_contents(app, u.contents, const_cast<hexenc<id>&>(s2->sha1sum));
          const_cast<unsigned&>(s2->size)=u.contents.size();
          file_contents=u.contents;
        }
      }
    }
    ticker();
  }
  ticker();
  debug();
}

void cvs_sync::sync(const std::string &repository, const std::string &module,
            const std::string &branch, app_state &app)
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
  
  cvs_sync::cvs_repository repo(repository,module);
  repo.GzipStream(3);
  transaction_guard guard(app.db);

  const cvs_sync::cvs_repository::tree_state_t &n=repo.now();
  
  repo.prime(app);
  
//  cert_revision_in_branch(merged, branch, app, dbw);
//  cert_revision_changelog(merged, log, app, dbw);
  
  guard.commit();      
//  P(F("[merged] %s\n") % merged);
}

#if 0
// fake to get it linking
#include "unit_tests.hh"
test_suite * init_unit_test_suite(int argc, char * argv[])
{ return 0;
}

#include <getopt.h>

int main(int argc,char **argv)
{ std::string repository="/usr/local/cvsroot";
  std::string module="christof/java";
  std::string host;
  std::string user;
  bool pserver=false;
  int compress_level=3;
  int c;
  while ((c=getopt(argc,argv,"z:d:v"))!=-1)
  { switch(c)
    { case 'z': compress_level=atoi(optarg);
        break;
      case 'd': 
        { std::string d_arg=optarg;
          unsigned len;
          if (cvs_client::begins_with(d_arg,":pserver:",len))
          { pserver=true;
            d_arg.erase(0,len);
          }
          std::string::size_type at=d_arg.find('@');
          std::string::size_type host_start=at;
          if (at!=std::string::npos) 
          { user=d_arg.substr(0,at); 
            ++host_start; 
          }
          else host_start=0;
          std::string::size_type colon=d_arg.find(':',host_start);
          std::string::size_type repo_start=colon;
          if (colon!=std::string::npos) 
          { host=d_arg.substr(host_start,colon-host_start); 
            ++repo_start; 
          }
          else repo_start=0;
          repository=d_arg.substr(repo_start);
        }
        break;
      case 'v': global_sanity.set_debug();
        break;
      default: 
        std::cerr << "USAGE: cvs_client [-z level] [-d repo] [module]\n";
        exit(1);
        break;
    }
  }
  if (optind+1<=argc) module=argv[optind];
  try
  { cvs_repository cl(host,repository,user,module,pserver);
    if (compress_level) cl.GzipStream(compress_level);
    const cvs_repository::tree_state_t &n=cl.now();
  } catch (std::exception &e)
  { std::cerr << e.what() << '\n';
  }
  return 0;
}
#endif
