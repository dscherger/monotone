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
  std::string module;
  
public:  
  cvs_client(const std::string &host, const std::string &root,
             const std::string &user=std::string(), 
             const std::string &module=std::string());
  ~cvs_client();
             
  void writestr(const std::string &s, bool flush=false);
  std::string readline();
  
  size_t get_bytes_read() const { return bytes_read; }
  size_t get_bytes_written() const { return bytes_written; }
  void ticker(bool newline=true) const
  { std::cerr << "[bytes in: " << bytes_read << "] [bytes out: " 
          << bytes_written << "]";
    if (newline) std::cerr << '\n';
  }
  void SendCommand(const char *cmd,...);
  // false if none available
  bool fetch_result(std::string &result);
  // semi internal helper to get one result line from a list
  static std::string combine_result(const std::list<std::pair<std::string,std::string> > &result);
  // MT style
  bool fetch_result(std::list<std::pair<std::string,std::string> > &result);
  void GzipStream(int level);
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
    inputbuffer=inputbuffer.substr(1);
    if (c=='\n') 
    { L(F("readline result '%s'\n") % result);
//std::cerr << "readline: \"" <<  result << "\"\n";
      return result;
    }
    else result+=c;
  }
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
  const char *arg;
  while ((arg=va_arg(ap,const char *)))
  { writestr("Argument "+std::string(arg)+"\n");
  }
  writestr(cmd+std::string("\n"));
}

static bool begins_with(const std::string &s, const std::string &sub)
{ return s.substr(0,sub.size())==sub;
}

cvs_client::cvs_client(const std::string &host, const std::string &root, 
                    const std::string &user, const std::string &_module)
    : readfd(-1), writefd(-1), bytes_read(0), bytes_written(0),
      gzip_level(0), module(_module)
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
  
  I(Valid_requests.find("UseUnchanged")!=Valid_requests.end());

  writestr("UseUnchanged\n"); // ???
  ticker();

//  writestr("Global_option -q\n"); // -Q?
//  GzipStream(3);
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
{ std::string cmd="Gzip-stream ";
  cmd+=char('0'+level);
  cmd+='\n';
  writestr(cmd);
  int error=deflateParams(&compress,level,Z_DEFAULT_STRATEGY);
  if (error!=Z_OK) throw std::runtime_error("deflateParams");
  gzip_level=level;
}

bool cvs_client::fetch_result(std::string &result)
{ std::list<std::pair<std::string,std::string> > res;
  if (!fetch_result(res) || res.empty()) return false;
  result=combine_result(res);
  return true;
}

std::string cvs_client::combine_result(const std::list<std::pair<std::string,std::string> > &res)
{ if (res.empty()) return std::string();
  // optimized for the single entry case
  std::list<std::pair<std::string,std::string> >::const_iterator i=res.begin();
  std::string result=i->second;
  for (++i;i!=res.end();++i) result+=i->second;
  return result;
}

bool cvs_client::fetch_result(std::list<std::pair<std::string,std::string> > &result)
{ result.clear();
loop:
  std::string x=readline();
  if (x.size()<2) goto error;
  if (begins_with(x,"E ")) 
  { std::cerr << x.substr(2) << '\n';
    goto loop;
  }
  if (begins_with(x,"M "))
  { result.push_back(std::make_pair(std::string(),x.substr(2)));
    return true;
  }
  if (x=="MT newline") return true;
  if (begins_with(x,"MT ")) 
  { std::string::size_type sep=x.find_first_of(" ",3);
    if (sep==std::string::npos) 
      result.push_back(std::make_pair(std::string(),x.substr(3)));
    else
      result.push_back(std::make_pair(x.substr(3,sep-3),x.substr(sep+1)));
    goto loop;
  }
  if (x=="ok") return false;
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

const cvs_repository::tree_state_t &cvs_repository::now()
{ if (edges.empty())
  { SendCommand("rlist","-l","-R","-d","--",module.c_str(),0);
    std::list<std::pair<std::string,std::string> > lresult;
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
          if (lresult.empty() || lresult.begin()->second.empty()) state=st_dir;
          else
          { I(lresult.size()==3);
            std::list<std::pair<std::string,std::string> >::const_iterator i0=lresult.begin();
            std::list<std::pair<std::string,std::string> >::const_iterator i1=i0;
            ++i1;
            std::list<std::pair<std::string,std::string> >::const_iterator i2=i1;
            ++i2;
            I(i0->first=="text");
            I(i1->first=="date");
            I(i2->first=="text");
            std::string keyword=trim(i0->second);
            std::string date=trim(i1->second);
            std::string version=trim(i2->second.substr(1,10));
            std::string dead=trim(i2->second.substr(12,4));
            std::string name=i2->second.substr(17);
            
            I(keyword[0]=='-' || keyword[0]=='d');
            I(dead.empty() || dead=="dead");
            I(!name.empty());
            
            if (keyword=="----") keyword=std::string();
            if (keyword!="d---")
            { //std::cerr << (directory+"/"+name) << " V" 
              //  << version << " from " << date << " " << dead
              //  << " " << keyword << '\n';
              time_t t=rls_l2time_t(date);
              files[directory+"/"+name].known_states.insert(file_state(t,version,!dead.empty()));
              edges.insert(cvs_edge(t));
            }
            // construct manifest
            // search for a matching revision 
            // - do that later when all files are known ???
          }
          break;
      }
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
  { std::cerr << i->first << " (" << i->second.known_states.size() << " states) ";
  }
  std::cerr << '\n';
  // tags map<string,map<string,string> >
  std::cerr << "Tags : ";
  for (std::map<std::string,std::map<std::string,std::string> >::const_iterator i=tags.begin();
      i!=tags.end();++i)
  { std::cerr << i->first << " (" << i->second.size() << " files) ";
  }
  std::cerr << '\n';
}

void cvs_repository::prime()
{ for (std::map<std::string,file>::iterator i=files.begin();i!=files.end();++i)
  { SendCommand("rlog","-b",i->first.c_str(),0);
    enum { st_head, st_tags, st_desc, st_rev, st_msg, st_date_author 
         } state=st_head;
    std::list<std::pair<std::string,std::string> > lresult;
    std::string revision;
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
          if (result.empty()) break; // accept a (first) empty line
          if (begins_with(result,"RCS file: ") ||
              begins_with(result,"head: ") ||
              begins_with(result,"branch:") ||
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
          std::map<std::string,std::string> &tagslot=tags[result.substr(1,colon-1)];
          tagslot[i->first]=result.substr(colon+2);
          break;
        }
        case st_desc:
        { std::string result=combine_result(lresult);
          if (result=="----------------------------")
          { state=st_rev;
            // i->second.= ???
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
          std::list<std::pair<std::string,std::string> >::const_iterator i0=lresult.begin();
          std::list<std::pair<std::string,std::string> >::const_iterator i1=i0;
          ++i1;
          std::list<std::pair<std::string,std::string> >::const_iterator i2=i1;
          ++i2;
          std::list<std::pair<std::string,std::string> >::const_iterator i3=i2;
          ++i3;
          std::list<std::pair<std::string,std::string> >::const_iterator i4=i3;
          ++i4;
          std::list<std::pair<std::string,std::string> >::const_iterator i5=i4;
          ++i5;
          I(i0->first=="text");
          I(i0->second=="date: ");
          I(i1->first=="date");
          checkin_time=rls_l2time_t(i1->second);
          I(i2->first=="text");
          I(i2->second==";  author: ");
          I(i3->first=="text");
          author=i3->second;
          I(i4->first=="text");
          I(i4->second==";  state: ");
          I(i5->first=="text");
          dead=i5->second;
          state=st_msg;
          break;
        }
        case st_msg:
        { std::string result=combine_result(lresult);
          // evtl überprüfen, ob das nicht nur ein fake war ...
          if (result=="----------------------------" ||
              result=="=============================================================================")
          { state=st_rev;
            std::pair<std::set<file_state>::iterator,bool> iter=
              i->second.known_states.insert
                (file_state(checkin_time,revision,dead=="dead"));
            // I(iter.second==false);
            // set iterators are read only to prevent you from destroying the order
            file_state &fs=const_cast<file_state &>(*(iter.first));
            fs.log_msg=message;
            edges.insert(cvs_edge(message,checkin_time,author));
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
