// copyright (C) 2005 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "cvs_sync.hh"

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
  std::cerr << " [files: " << files.size() 
          << "] [edges: " << edges.size() 
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
  return (--edges.end())->files; // wrong of course
}

void cvs_repository::debug() const
{ // edges set<cvs_edge>
  std::cerr << "Edges : ";
  for (std::set<cvs_edge>::const_iterator i=edges.begin();
      i!=edges.end();++i)
  { std::cerr << "[" << i->time;
    if (i->time!=i->time2) std::cerr << '+' << (i->time2-i->time);
    std::cerr << ',' << i->author << ',' 
      << i->changelog.size() << "] ";
  }
  std::cerr << '\n';
  // files map<string,file>
  std::cerr << "Files : ";
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
      else if (!j->contents.empty()) std::cerr << j->contents.size();
      else if (!j->rcs_patch.empty()) std::cerr << 'p' << j->rcs_patch.size();
      ++j;
      if (j!=i->second.known_states.end()) std::cerr << ',';
    }
    std::cerr << ") ";
  }
  std::cerr << '\n';
  // tags map<string,map<string,string> >
  std::cerr << "Tags : ";
  for (std::map<std::string,std::map<std::string,std::string> >::const_iterator i=tags.begin();
      i!=tags.end();++i)
  { std::cerr << i->first << "(" << i->second.size() << " files) ";
  }
  std::cerr << '\n';
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
  for (std::set<cvs_edge>::iterator i=edges.begin();i!=edges.end();++i)
  { std::set<cvs_edge>::iterator j=i;
    j++; // next one
    if (j==edges.end()) break;
    
    I(j->time2==j->time); // make sure we only do this once
    I(i->time2<=j->time); // should be sorted ...
    if (!i->similar_enough(*j)) 
      continue;
    I(i->time2<j->time); // should be non overlapping ...
    const_cast<time_t&>(i->time2)=j->time;
    edges.erase(j);
  }
  
  // get the contents
  for (std::map<std::string,file>::iterator i=files.begin();i!=files.end();++i)
  { I(!i->second.known_states.empty());
    { std::set<file_state>::iterator s2=i->second.known_states.begin();
      std::string revision=s2->cvs_version;
      cvs_client::checkout c=CheckOut(i->first,revision);
//    I(c.mod_time==?);
      const_cast<std::string &>(s2->contents)=c.contents;
      const_cast<bool&>(s2->dead)=c.dead;
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
        // if (c.dead) const_cast<bool&>(s2->dead)=true;
        const_cast<std::string &>(s2->contents)=c.contents;
      }
      else
      { cvs_client::update u=Update(i->first,s->cvs_version,s2->cvs_version);
        if (u.removed)
        { const_cast<bool&>(s2->dead)=true;
        }
        else if (!u.checksum.empty())
        { const_cast<std::string&>(s2->rcs_patch)=u.patch;
          const_cast<std::string&>(s2->sha1sum)=u.checksum;
        }
        else
          const_cast<std::string&>(s2->contents)=u.contents;
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
  cvs_sync::cvs_repository repo(repository,module);
  // repo.GzipStream(3); ?
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
