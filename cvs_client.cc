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
//#include <boost/tokenizer.hpp>
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
  void ticker(bool newline=true)
  { std::cerr << "[bytes in: " << bytes_read << "] [bytes out: " 
          << bytes_written << "]";
    if (newline) std::cerr << '\n';
  }
  void SendCommand(const char *cmd,...);
  // false if none available
  bool fetch_result(std::string &result);
  // semi internal helper to get one result line from a list
  std::string combine_result(const std::list<std::pair<std::string,std::string> > &result);
  // MT style
  bool fetch_result(std::list<std::pair<std::string,std::string> > &result);
  void GzipStream(int level);
};

void cvs_client::SendCommand(const char *cmd,...)
{ va_list ap;
  va_start(ap, cmd);
  const char *arg;
  while ((arg=va_arg(ap,const char *)))
  { writestr("Argument "+std::string(arg)+"\n");
  }
  writestr(cmd+std::string("\n"));
}

struct cvs_file_state
{ std::string revision;
  time_t last_changed;
};

struct cvs_changeset // == cvs_key ?? rcs_delta+rcs_deltatext
{ typedef std::map<std::string,cvs_file_state> tree_state_t;

//  cvs_client::stringset_t tags; ???
  tree_state_t tree_state; // dead files do not occur here
};

namespace { // cvs_key?
namespace constants
{ const static int cvs_window=5; }
struct cvs_edge // careful!
{ // std::string branch;
  std::string changelog;
  std::string author;
  time_t time; //  std::string time;
 
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
  std::list<tree_state_t> tree_states;
  // zusammen mit changelog, date, author(?)
  std::map<tree_state_t*,tree_state_t*> successor;
public:  
  cvs_repository(const std::string &host, const std::string &root,
             const std::string &user=std::string(), 
             const std::string &module=std::string())
      : cvs_client(host,root,user,module) {}

  std::list<std::string> get_modules();
  void set_branch(const std::string &tag);
  const tree_state_t &now();
  const tree_state_t &find(const std::string &date,const std::string &changelog);
  const tree_state_t &next(const tree_state_t &m) const;
};

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
    {
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
  I(answer.substr(0,15)=="Valid-requests ");
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
  inflateEnd(&uncompress);
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
  if (x.substr(0,2)=="E ") 
  { std::cerr << x.substr(2) << '\n';
    goto loop;
  }
  if (x.substr(0,2)=="M ") 
  { result.push_back(std::make_pair(std::string(),x.substr(2)));
    return true;
  }
  if (x=="MT newline") return true;
  if (x.substr(0,3)=="MT ") 
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
  struct tm tm;
  memset(&tm,0,sizeof tm);
  tm.tm_year=atoi(t.substr(0,4).c_str());
  tm.tm_mon=atoi(t.substr(5,2).c_str());
  tm.tm_mday=atoi(t.substr(8,2).c_str());
  tm.tm_hour=atoi(t.substr(11,2).c_str());
  tm.tm_min=atoi(t.substr(14,2).c_str());
  tm.tm_sec=atoi(t.substr(17,2).c_str());
  int dst_offs=atoi(t.substr(20,5).c_str());
  I(!dst_offs);
#if 0 // non portable
  return timegm(&tm);
#else // ugly
  putenv("TZ=UTC"); // or setenv("TZ","UTC",true);
  // do we need to call tzset? (docs tend to say no)
  return mktime(&tm);
#endif
}

const cvs_repository::tree_state_t &cvs_repository::now()
{ if (tree_states.empty())
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
            I(date[4]=='-' && date[7]=='-');
            I(date[10]==' ' && date[13]==':');
            I(date[16]==':' && date[19]==' ');
            I(date[20]=='+' || date[20]=='-');
            I(dead.empty() || dead=="dead");
            I(!name.empty());
            
            if (keyword=="----") keyword=std::string();
            if (keyword!="d---")
            { std::cerr << (directory+"/"+name) << " V" 
                << version << " from " << date << " " << dead
                << " " << keyword << '\n';
            }
          }
          break;
      }
    }
    ticker();
  }
  return tree_states.back(); // wrong of course
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
  while ((c=getopt(argc,argv,"z:d:"))!=-1)
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
