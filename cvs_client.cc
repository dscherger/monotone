// copyright (C) 2005 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

//#include <sys/types.h>
#include <unistd.h>
#include <map>
#include <string>
#include <list>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <set>
#include <stdarg.h>
#include <zlib.h>
#include <fcntl.h>
#include <cassert>
#include "sanity.hh"
//#include "rcs_file.hh"

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

struct rlog_callbacks
{ // virtual void file(const std::string &file,)=0;
  virtual void file(const std::string &file,
        const std::string &head_rev) const=0;
  virtual void tag(const std::string &file,const std::string &tag, 
        const std::string &revision) const=0;
  virtual void revision(const std::string &file,time_t checkin_date,
        const std::string &rev,const std::string &author,
        const std::string &state,const std::string &log) const=0;
};

struct rlist_callbacks
{ virtual void file(const std::string &name, time_t last_change,
        const std::string &last_rev, bool dead) const=0;
};

class cvs_client
{ int readfd,writefd;
  size_t bytes_read,bytes_written;
  typedef std::set<std::string> stringset_t;
  stringset_t Valid_requests;
  int gzip_level;
  z_stream compress,decompress;
  std::string inputbuffer;

  void InitZipStream(int level);
  void underflow(); // fetch new characters from stream
protected:
  std::string root;
  std::string module;
  
public:  
  cvs_client(const std::string &host, const std::string &_root,
             const std::string &user=std::string(), 
             const std::string &module=std::string());
  ~cvs_client();
             
  void writestr(const std::string &s, bool flush=false);
  std::string readline();
  std::string read_n(unsigned size);
  
  size_t get_bytes_read() const { return bytes_read; }
  size_t get_bytes_written() const { return bytes_written; }
  void ticker(bool newline=true) const
  { std::cerr << "[bytes in: " << bytes_read << "] [bytes out: " 
          << bytes_written << "]";
    if (newline) std::cerr << '\n';
  }
  void SendCommand(const char *cmd,...);
  void SendCommand(const char *cmd, va_list ap);
  // false if none available
  bool fetch_result(std::string &result);
  // semi internal helper to get one result line from a list
  static std::string combine_result(const std::vector<std::pair<std::string,std::string> > &result);
  // MT style
  bool fetch_result(std::vector<std::pair<std::string,std::string> > &result);
  void GzipStream(int level);
  
  void RLog(const rlog_callbacks &cb,bool dummy,...);
  void RList(const rlist_callbacks &cb,bool dummy,...);
  
  bool CommandValid(const std::string &cmd) const
  { return Valid_requests.find(cmd)!=Valid_requests.end(); }
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
    // nb: this must sort as > to construct the edges in the right direction
    return time > other.time ||

      (time == other.time 
       && author > other.author) ||

      (time == other.time 
       && author == other.author 
       && changelog > other.changelog);
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

// copied from netsync.cc from the ssh branch
static pid_t pipe_and_fork(int *fd1,int *fd2)
{ pid_t result=-1;
  fd1[0]=-1; fd1[1]=-1;
  fd2[0]=-1; fd2[1]=-1;
#ifndef __WIN32__
  if (pipe(fd1)) return -1;
  if (pipe(fd2)) 
  { close(fd1[0]); close(fd1[1]); return -1; }
  result=fork();
  if (result<0)
  { close(fd1[0]); close(fd1[1]);
    close(fd2[0]); close(fd2[1]);
    return -1;
  }
  else if (!result)
  { // fd1[1] for writing, fd2[0] for reading
    close(fd1[0]);
    close(fd2[1]);
    if (dup2(fd2[0],0)!=0 || dup2(fd1[1],1)!=1) 
    { perror("dup2");
      exit(-1); // kill the useless child
    }
    close(fd1[1]);
    close(fd2[0]);
  }
  else
  { // fd1[0] for reading, fd2[1] for writing
    close(fd1[1]);
    close(fd2[0]);
  }
#endif  
  return result;
}

void cvs_client::writestr(const std::string &s, bool flush)
{ if (!gzip_level)
  { if (s.size()) bytes_written+=write(writefd,s.c_str(),s.size());
    return;
  }
  char outbuf[1024];
  compress.next_in=(Bytef*)s.c_str();
  compress.avail_in=s.size();
  for (;;)
  // the zlib headers say that avail_out is the only criterion for looping
  { compress.next_out=(Bytef*)outbuf;
    compress.avail_out=sizeof outbuf;
    int err=deflate(&compress,flush?Z_SYNC_FLUSH:Z_NO_FLUSH);
    if (err!=Z_OK && err!=Z_BUF_ERROR) 
    { std::cerr << "deflate err " << err << '\n';
      throw std::runtime_error("deflate");
    }
    unsigned written=sizeof(outbuf)-compress.avail_out;
    if (written) bytes_written+=write(writefd,outbuf,written);
    else break;
  }
}

std::string cvs_client::readline()
{ // flush
  writestr(std::string(),true);

  // read input
  std::string result;
  for (;;)
  { if (inputbuffer.empty()) underflow(); 
    if (inputbuffer.empty()) throw std::runtime_error("no data avail");
    char c=inputbuffer[0];
    inputbuffer.erase(0,1); // =inputbuffer.substr(1);
    if (c=='\n') 
    { L(F("readline result '%s'\n") % result);
//std::cerr << "readline: \"" <<  result << "\"\n";
      return result;
    }
    else result+=c;
  }
}

std::string cvs_client::read_n(unsigned len)
{ // no flush necessary
  std::string result;
  while (len)
  { if (inputbuffer.empty()) underflow(); 
    I(!inputbuffer.empty());
    unsigned avail=inputbuffer.size();
    if (len<avail) avail=len;
    result+=inputbuffer.substr(0,avail);
    inputbuffer.erase(0,avail);
    len-=avail;
  }
  return result;
}

// are there chars available? get them block if none is available, then
// get as much as possible
void cvs_client::underflow()
{ char buf[1024],buf2[1024];
try_again:
  if (read(readfd,buf,1)!=1) throw std::runtime_error("read error");
  unsigned avail_in=1;
  fcntl(readfd,F_SETFL,fcntl(readfd,F_GETFL)|O_NONBLOCK);
  avail_in+=read(readfd,buf+1,sizeof(buf)-1);
  fcntl(readfd,F_SETFL,fcntl(readfd,F_GETFL)&~O_NONBLOCK);
  bytes_read+=avail_in;
  if (!gzip_level)
  { inputbuffer+=std::string(buf,buf+avail_in);
    return;
  }
  decompress.next_in=(Bytef*)buf;
  decompress.avail_in=avail_in;
  for (;;)
  { decompress.next_out=(Bytef*)buf2;
    decompress.avail_out=sizeof(buf2);
    int err=inflate(&decompress,Z_NO_FLUSH);
    if (err!=Z_OK && err!=Z_BUF_ERROR) 
    { std::cerr << "inflate err " << err << '\n';
      throw std::runtime_error("inflate");
    }
    unsigned bytes_in=sizeof(buf2)-decompress.avail_out;
    if (bytes_in) inputbuffer+=std::string(buf2,buf2+bytes_in);
    else break;
  }
  if (inputbuffer.empty()) goto try_again;
}

// this mutable/const oddity is to avoid an 
// "invalid initialization of non-const reference from a temporary" warning
// when passing this class to stringtok (we cheat by using a const reference)
template <typename Container>
 class push_back2insert_cl
{ mutable Container &c;
public:
  push_back2insert_cl(Container &_c) : c(_c) {}
  template <typename T>
   void push_back(const T &t) const { c.insert(t); }
};

// the creator function (so you don't need to specify the type
template <typename Container>
 const push_back2insert_cl<Container> push_back2insert(Container &c)
{ return push_back2insert_cl<Container>(c);
}

// inspired by code from Marcelo E. Magallon and the libstdc++ doku
template <typename Container>
void
stringtok (Container &container, std::string const &in,
           const char * const delimiters = " \t\n")
{
    const std::string::size_type len = in.length();
          std::string::size_type i = 0;

    while ( i < len )
    {
        // eat leading whitespace
        // i = in.find_first_not_of (delimiters, i);
        // if (i == std::string::npos)
        //    return;   // nothing left but white space

        // find the end of the token
        std::string::size_type j = in.find_first_of (delimiters, i);

        // push token
        if (j == std::string::npos) {
            container.push_back (in.substr(i));
            return;
        } else
            container.push_back (in.substr(i, j-i));

        // set up for next loop
        i = j + 1;
    }
}

// "  AA  " s=2 e=3
std::string trim(const std::string &s)
{ std::string::size_type start=s.find_first_not_of(" ");
  if (start==std::string::npos) return std::string();
  std::string::size_type end=s.find_last_not_of(" ");
  if (end==std::string::npos) end=s.size();
  else ++end;
  return s.substr(start,end-start);
}

void cvs_client::SendCommand(const char *cmd,...)
{ va_list ap;
  va_start(ap, cmd);
  SendCommand(cmd,ap);
  va_end(ap);
}

void cvs_client::SendCommand(const char *cmd,va_list ap)
{ const char *arg;
  while ((arg=va_arg(ap,const char *)))
  { writestr("Argument "+std::string(arg)+"\n");
  }
  writestr(cmd+std::string("\n"));
}

static bool begins_with(const std::string &s, const std::string &sub, unsigned &len)
{ if (s.substr(0,sub.size())==sub) { len=sub.size(); return true; }
  return false;
}

static bool begins_with(const std::string &s, const std::string &sub)
{ return s.substr(0,sub.size())==sub;
}

cvs_client::cvs_client(const std::string &host, const std::string &_root, 
                    const std::string &user, const std::string &_module)
    : readfd(-1), writefd(-1), bytes_read(0), bytes_written(0),
      gzip_level(0), root(_root), module(_module)
{ memset(&compress,0,sizeof compress);
  memset(&decompress,0,sizeof decompress);
  int fd1[2],fd2[2];
  pid_t child=pipe_and_fork(fd1,fd2);
  if (child<0) 
  {  throw std::runtime_error("pipe/fork failed");
  }
  else if (!child)
  { const unsigned newsize=64;
    const char *newargv[newsize];
    unsigned newargc=0;
    if (host.empty())
    { newargv[newargc++]="cvs";
      newargv[newargc++]="server";
    }
    else
    { const char *rsh=getenv("CVS_RSH");
      if (!rsh) rsh="rsh";
      newargv[newargc++]=rsh;
      if (!user.empty())
      { newargv[newargc++]="-l";
        newargv[newargc++]=user.c_str();
      }
      newargv[newargc++]=host.c_str();
      newargv[newargc++]="cvs server";
    }
    newargv[newargc]=0;
    
    execvp(newargv[0],const_cast<char*const*>(newargv));
    perror(newargv[0]);
    exit(errno);
  }
  readfd=fd1[0];
  writefd=fd2[1];
  
  InitZipStream(0);
  writestr("Root "+root+"\n");
  writestr("Valid-responses ok error Valid-requests Checked-in "
              "New-entry Checksum Copy-file Updated Created Update-existing "
              "Merged Patched Rcs-diff Mode Mod-time Removed Remove-entry "
              "Set-static-directory Clear-static-directory Set-sticky "
              "Clear-sticky Template Clear-template Notified Module-expansion "
              "Wrapper-rcsOption M Mbinary E F MT\n");

  writestr("valid-requests\n");
  std::string answer=readline();
  I(begins_with(answer,"Valid-requests "));
  // boost::tokenizer does not provide the needed functionality (e.g. preserve -)
  stringtok(push_back2insert(Valid_requests),answer.substr(15));
  answer=readline();
  I(answer=="ok");
  
  I(CommandValid("UseUnchanged"));

  writestr("UseUnchanged\n"); // ???
  ticker();

//  writestr("Global_option -q\n"); // -Q?
}

cvs_client::~cvs_client()
{ deflateEnd(&compress);
  inflateEnd(&decompress);
}

void cvs_client::InitZipStream(int level)
{ int error=deflateInit(&compress,level);
  if (error!=Z_OK) throw std::runtime_error("deflateInit");
  error=inflateInit(&decompress);
  if (error!=Z_OK) throw std::runtime_error("inflateInit");
}

void cvs_client::GzipStream(int level)
{ if (!CommandValid("Gzip-stream")) return;
  std::string cmd="Gzip-stream ";
  cmd+=char('0'+level);
  cmd+='\n';
  writestr(cmd);
  int error=deflateParams(&compress,level,Z_DEFAULT_STRATEGY);
  if (error!=Z_OK) throw std::runtime_error("deflateParams");
  gzip_level=level;
}

bool cvs_client::fetch_result(std::string &result)
{ std::vector<std::pair<std::string,std::string> > res;
  if (!fetch_result(res) || res.empty()) return false;
  result=combine_result(res);
  return true;
}

std::string cvs_client::combine_result(const std::vector<std::pair<std::string,std::string> > &res)
{ if (res.empty()) return std::string();
  // optimized for the single entry case
  std::vector<std::pair<std::string,std::string> >::const_iterator i=res.begin();
  std::string result=i->second;
  for (++i;i!=res.end();++i) result+=i->second;
  return result;
}

bool cvs_client::fetch_result(std::vector<std::pair<std::string,std::string> > &result)
{ result.clear();
  std::list<std::string> active_tags;
loop:
  std::string x=readline();
  unsigned len=0;
  if (x.size()<2) goto error;
  if (begins_with(x,"E ",len)) 
  { std::cerr << x.substr(len) << '\n';
    goto loop;
  }
  if (begins_with(x,"M ",len))
  { result.push_back(std::make_pair(std::string(),x.substr(len)));
    return true;
  }
  if (active_tags.empty() && x=="MT newline") return true;
  if (begins_with(x,"MT ",len)) 
  { if (x[len]=='+') 
    { active_tags.push_back(x.substr(len+1));
      result.push_back(std::make_pair(std::string(),x.substr(len)));
      goto loop;
    }
    if (x[len]=='-') 
    { I(!active_tags.empty());
      I(active_tags.back()==x.substr(len+1));
      active_tags.pop_back();
      result.push_back(std::make_pair(std::string(),x.substr(len)));
      if (active_tags.empty()) return true;
      goto loop;
    }
    std::string::size_type sep=x.find_first_of(" ",len);
    if (sep==std::string::npos) 
      result.push_back(std::make_pair(std::string(),x.substr(len)));
    else
      result.push_back(std::make_pair(x.substr(len,sep-len),x.substr(sep+1)));
    goto loop;
  }
  if (x=="ok") return false;
  if (!result.empty()) goto error;
  // more complex results
  if (begins_with(x,"Clear-sticky ",len) 
      || begins_with(x,"Set-static-directory ",len))
  { result.push_back(std::make_pair("CMD",x.substr(0,len-1)));
    result.push_back(std::make_pair("dir",x.substr(len)));
    result.push_back(std::make_pair("rcs",readline()));
    return true;
  }
  if (begins_with(x,"Mod-time ",len))
  { result.push_back(std::make_pair("CMD",x.substr(0,len-1)));
    result.push_back(std::make_pair("date",x.substr(len)));
    return true;
  }
  if (begins_with(x,"Created ",len))
  { result.push_back(std::make_pair("CMD",x.substr(0,len-1)));
    result.push_back(std::make_pair("dir",x.substr(len)));
    result.push_back(std::make_pair("rcs",readline()));
    result.push_back(std::make_pair("new entries line",readline()));
    result.push_back(std::make_pair("mode",readline()));
    std::string length=readline();
    result.push_back(std::make_pair("length",length));
    result.push_back(std::make_pair("data",read_n(atol(length.c_str()))));
    return true;
  }
error:
  std::cerr << "unrecognized result \"" << x << "\"\n";
  exit(1);
}

static time_t rls_l2time_t(const std::string &t)
{ // 2003-11-26 09:20:57 +0000
  I(t[4]=='-' && t[7]=='-');
  I(t[10]==' ' && t[13]==':');
  I(t[16]==':' && t[19]==' ');
  I(t[20]=='+' || t[20]=='-');
  struct tm tm;
  memset(&tm,0,sizeof tm);
  tm.tm_year=atoi(t.substr(0,4).c_str())-1900;
  tm.tm_mon=atoi(t.substr(5,2).c_str())-1;
  tm.tm_mday=atoi(t.substr(8,2).c_str());
  tm.tm_hour=atoi(t.substr(11,2).c_str());
  tm.tm_min=atoi(t.substr(14,2).c_str());
  tm.tm_sec=atoi(t.substr(17,2).c_str());
  int dst_offs=atoi(t.substr(20,5).c_str());
//  L(F("%d-%d-%d %d:%02d:%02d %04d") % tm.tm_year % tm.tm_mon % tm.tm_mday 
//    % tm.tm_hour % tm.tm_min % tm.tm_sec % dst_offs );
  tm.tm_isdst=0;
  I(!dst_offs);
  time_t result=-1;
#if 0 // non portable
  result=timegm(&tm);
#else // ugly
  const char *tz=getenv("TZ");
  setenv("TZ","",true);
  tzset();
  result=mktime(&tm);
  if (tz) setenv("TZ", tz, true);
  else unsetenv("TZ");
  tzset();
#endif
//  L(F("result %ld\n") % result);
  return result;
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

void cvs_client::RList(const rlist_callbacks &cb,bool dummy,...)
{ { va_list ap;
    va_start(ap,dummy);
    SendCommand("rlist",ap);
    va_end(ap);
  }
  std::vector<std::pair<std::string,std::string> > lresult;
  enum { st_dir, st_file } state=st_dir;
  std::string directory;
  while (fetch_result(lresult))
  { switch(state)
    { case st_dir:
      { std::string result=combine_result(lresult);
        I(result.size()>=2);
        I(result[result.size()-1]==':');
        directory=result.substr(0,result.size()-1);
        state=st_file;
        ticker();
        break;
      }
      case st_file:
        if (lresult.empty() || lresult[0].second.empty()) state=st_dir;
        else
        { I(lresult.size()==3);
          I(lresult[0].first=="text");
          I(lresult[1].first=="date");
          I(lresult[2].first=="text");
          std::string keyword=trim(lresult[0].second);
          std::string date=trim(lresult[1].second);
          std::string version=trim(lresult[2].second.substr(1,10));
          std::string dead=trim(lresult[2].second.substr(12,4));
          std::string name=lresult[2].second.substr(17);
          
          I(keyword[0]=='-' || keyword[0]=='d');
          I(dead.empty() || dead=="dead");
          I(!name.empty());
          
          if (keyword=="----") keyword=std::string();
          if (keyword!="d---")
          { //std::cerr << (directory+"/"+name) << " V" 
            //  << version << " from " << date << " " << dead
            //  << " " << keyword << '\n';
            time_t t=rls_l2time_t(date);
            cb.file(directory+"/"+name,t,version,!dead.empty());
          }
          // construct manifest
          // search for a matching revision 
          // - do that later when all files are known ???
        }
        break;
    }
  }
}

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
    if (begins_with(i->first,module,len))
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

// dummy is needed to satisfy va_start (cannot pass objects of non-POD type)
void cvs_client::RLog(const rlog_callbacks &cb,bool dummy,...)
{ { va_list ap;
    va_start(ap,dummy);
    SendCommand("rlog",ap);
    va_end(ap);
  }
  static const char * const fileend="=============================================================================";
  static const char * const revisionend="----------------------------";
  enum { st_head, st_tags, st_desc, st_rev, st_msg, st_date_author 
       } state=st_head;
  std::vector<std::pair<std::string,std::string> > lresult;
  std::string file;
  std::string revision,head_rev;
  std::string message;
  std::string author;
  std::string description;
  std::string dead;
  time_t checkin_time=0;
  while (fetch_result(lresult))
  {reswitch:
    L(F("state %d\n") % int(state));
    switch(state)
    { case st_head:
      { std::string result=combine_result(lresult);
        unsigned len;
        if (result.empty()) break; // accept a (first) empty line
        if (result==fileend)
        { cb.file(file,head_rev);
        }
        else if (begins_with(result,"RCS file: ",len))
        { I(result.substr(len,root.size())==root);
          if (result[len+root.size()]=='/') ++len;
          file=result.substr(len+root.size());
          if (file.substr(file.size()-2)==",v") file.erase(file.size()-2,2);
        }
        else if (begins_with(result,"head: ",len))
        { head_rev=result.substr(len);
        }
        else if (begins_with(result,"branch:") ||
            begins_with(result,"locks: ") ||
            begins_with(result,"access list:") ||
            begins_with(result,"keyword substitution: ") ||
            begins_with(result,"total revisions: "))
          ;
        else if (result=="description:")
          state=st_desc;
        else if (result=="symbolic names:")
          state=st_tags;
        else
        { std::cerr << "unknown rcs head '" << result << "'\n";
        }
        break;
      }
      case st_tags:
      { std::string result=combine_result(lresult);
        I(!result.empty());
        if (result[0]!='\t') 
        { L(F("result[0] %d %d\n") % result.size() % int(result[0])); state=st_head; goto reswitch; }
        I(result.find_first_not_of("\t ")==1);
        std::string::size_type colon=result.find(':');
        I(colon!=std::string::npos);
        cb.tag(file,result.substr(1,colon-1),result.substr(colon+2));
        break;
      }
      case st_desc:
      { std::string result=combine_result(lresult);
        if (result==revisionend)
        { state=st_rev;
          // cb.file(file,description);
        }
        else
        { if (!description.empty()) description+='\n';
          description+=result;
        }
        break;
      }
      case st_rev:
      { std::string result=combine_result(lresult);
        I(begins_with(result,"revision "));
        revision=result.substr(9);
        state=st_date_author;
        break;
      }
      case st_date_author:
      { I(lresult.size()==11 || lresult.size()==7);
        I(lresult[0].first=="text");
        I(lresult[0].second=="date: ");
        I(lresult[1].first=="date");
        checkin_time=rls_l2time_t(lresult[1].second);
        I(lresult[2].first=="text");
        I(lresult[2].second==";  author: ");
        I(lresult[3].first=="text");
        author=lresult[3].second;
        I(lresult[4].first=="text");
        I(lresult[4].second==";  state: ");
        I(lresult[5].first=="text");
        dead=lresult[5].second;
        state=st_msg;
        break;
      }
      case st_msg:
      { std::string result=combine_result(lresult);
        // evtl überprüfen, ob das nicht nur ein fake war ...
        if (result==revisionend || result==fileend)
        { cb.revision(file,checkin_time,revision,author,dead,message);
          if (result==fileend) 
          { state=st_head;
            goto reswitch; // emit file cb
          }
          state=st_rev;
        }
        else
        { if (!message.empty()) message+='\n';
          message+=result;
        }
        break;
      }
    }
  }
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
  }
  // remove duplicate states
  for (std::set<cvs_edge>::iterator i=edges.begin();i!=edges.end();)
  { if (i->changelog_valid || i->author.size()) { ++i; continue; }
    std::set<cvs_edge>::iterator j=i;
    I(j!=edges.begin());
    --j;
    I(j->time==i->time);
    I(i->files.empty());
    I(i->revision.empty());
    j=i; // why does erase not return the next iter :-(
    ++i;
    edges.erase(j); 
  }
  // join adjacent check ins (same author, same changelog)
  
  // get the contents
  for (std::map<std::string,file>::iterator i=files.begin();i!=files.end();++i)
  { I(!i->second.known_states.empty());
    std::string revision=i->second.known_states.begin()->cvs_version;
    SendCommand("Directory .",/*"-N","-P",*/"-r",revision.c_str(),"--",i->first.c_str(),0);
    writestr(root+"\n");
    writestr("co\n");
    enum { st_co
         } state=st_co;
    std::vector<std::pair<std::string,std::string> > lresult;
    std::string dir,dir2,rcsfile,mode;
    time_t stamp=-1;
    while (fetch_result(lresult))
    { switch(state)
      { case st_co:
        { I(!lresult.empty());
          if (lresult[0].first=="CMD")
          { if (lresult[0].second=="Clear-sticky")
            { I(lresult.size()==3);
              I(lresult[1].first=="dir");
              dir=lresult[1].second;
            }
            else if (lresult[0].second=="Set-static-directory")
            { I(lresult.size()==3);
              I(lresult[1].first=="dir");
              dir2=lresult[1].second;
            }
            else if (lresult[0].second=="Mod-time")
            { I(lresult.size()==2);
              I(lresult[1].first=="date");
              // this is 18 Nov 1996 14:39:40 -0000 format - strange ...
              // stamp=rls_l2time_t(lresult[1].second);
            }
            else if (lresult[0].second=="Created")
            { // std::cerr << combine_result(lresult) << '\n';
              I(lresult.size()==7);
              I(lresult[6].first=="data");
              const_cast<std::string &>(i->second.known_states.begin()->contents)=lresult[6].second;
              L(F("file %s revision %s: %d bytes\n") % i->first 
                  % revision % lresult[6].second.size());
            }
            else
            { std::cerr << "unrecognized response " << lresult[0].second << '\n';
            }
          }
          else if (lresult[0].second=="+updated")
          { // std::cerr << combine_result(lresult) << '\n';
          }
          else 
          { std::cerr << "unrecognized response " << lresult[0].second << '\n';
          }
          break;
        }
      }
    }
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
