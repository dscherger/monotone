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

struct cvs_revision
{ std::vector<int> parts;

  cvs_revision(const std::string &x);
  void operator++(int);
  std::string get_string() const;
  bool is_branch() const;
  bool is_parent_of(const cvs_revision &child) const;
};

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

namespace { // cvs_key?
namespace constants
{ const static int cvs_window=5; }
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
};}

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
             const std::string &module=std::string())
      : cvs_client(host,root,user,module) {}

  std::list<std::string> get_modules();
  void set_branch(const std::string &tag);
  void ticker() const;
  const tree_state_t &now();
  const tree_state_t &find(const std::string &date,const std::string &changelog);
  const tree_state_t &next(const tree_state_t &m) const;
  
  void debug() const;
};

//--------------------- implementation -------------------------------

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
    prime();
    ticker();
    debug();
  }
  return (--edges.end())->files; // wrong of course
}

void cvs_repository::debug() const
{ // edges set<cvs_edge>
  std::cerr << "Edges : ";
  for (std::set<cvs_edge>::const_iterator i=edges.begin();
      i!=edges.end();++i)
  { std::cerr << "[" << i->time << ',' << i->author << ',' 
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
    { if (!j->contents.empty()) std::cerr << j->contents.size();
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
  std::map<std::string,struct ::file>::iterator i;
  prime_log_cb(cvs_repository &r,const std::map<std::string,struct ::file>::iterator &_i) 
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

void cvs_repository::prime()
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
  // join adjacent check ins (same author, same changelog)
  
  // get the contents
  for (std::map<std::string,file>::iterator i=files.begin();i!=files.end();++i)
  { I(!i->second.known_states.empty());
    std::string revision=i->second.known_states.begin()->cvs_version;
    struct checkout c=CheckOut(i->first,revision);
//    I(c.mod_time==?);
    const_cast<std::string &>(i->second.known_states.begin()->contents)=c.contents;
    for (std::set<file_state>::iterator s=i->second.known_states.begin();
          s!=i->second.known_states.end();++s)
    { std::set<file_state>::iterator s2=s;
      ++s2;
      if (s2==i->second.known_states.end()) break;
      cvs_revision srev(s->cvs_version);
      I(srev.is_parent_of(s2->cvs_version));
      cvs_client::update u=Update(i->first,s->cvs_version,s2->cvs_version);
      if (u.removed)
      { // ???
      }
      else if (!u.checksum.empty())
      { const_cast<std::string&>(s2->rcs_patch)=u.patch;
        const_cast<std::string&>(s2->sha1sum)=u.checksum;
      }
      else
        const_cast<std::string&>(s2->contents)=u.contents;
    }
    ticker();
  }
}

#if 1
// fake to get it linking
#include "unit_tests.hh"
test_suite * init_unit_test_suite(int argc, char * argv[])
{ return 0;
}

#include <getopt.h>

int main(int argc,char **argv)
{ std::string repository="/usr/local/cvsroot";
  std::string module="christof/java";
  std::string host="";
  std::string user="";
  int compress_level=3;
  int c;
  while ((c=getopt(argc,argv,"z:d:v"))!=-1)
  { switch(c)
    { case 'z': compress_level=atoi(optarg);
        break;
      case 'd': 
        { std::string d_arg=optarg;
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
  { cvs_repository cl(host,repository,user,module);
    if (compress_level) cl.GzipStream(compress_level);
    const cvs_repository::tree_state_t &n=cl.now();
  } catch (std::exception &e)
  { std::cerr << e.what() << '\n';
  }
  return 0;
}
#endif
