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
#include <cassert>
#include <stdexcept>
#include <set>
#include <stdarg.h>
#include <zlib.h>
#include <fcntl.h>
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
  while (compress.avail_in || flush)
  // the zlib headers say that avail_out is the only criterion for 
  // looping, I find testing avail_in more logical
  { compress.next_out=(Bytef*)outbuf;
    compress.avail_out=sizeof outbuf;
    int err=deflate(&compress,flush?Z_SYNC_FLUSH:Z_NO_FLUSH);
    if (err!=Z_OK) throw std::runtime_error("deflate");
    unsigned written=sizeof(outbuf)-compress.avail_out;
    if (written) bytes_written+=write(writefd,outbuf,written);
    else break;
  }
}

// TODO: optimize
std::string cvs_client::readline()
{ // flush
  writestr(std::string(),true);

  // read input
  std::string result;
  for (;;)
  { if (inputbuffer.empty()) 
    { underflow(); 
//std::cerr << inputbuffer.size() << " bytes available\n";
    }
    else
//std::cerr << inputbuffer.size() << " bytes still left\n";
    if (inputbuffer.empty()) throw std::runtime_error("no data avail");
    char c=inputbuffer[0];
    inputbuffer=inputbuffer.substr(1);
    if (c=='\n') 
    {
std::cerr << "readline: \"" <<  result << "\"\n";
      return result;
    }
    else result+=c;
  }
}

// are there chars available? get them block if none is available, then
// get as much as possible
void cvs_client::underflow()
{ char buf[1024],buf2[1024];
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
    if (err!=Z_OK) throw std::runtime_error("inflate");
    unsigned bytes_in=sizeof(buf2)-decompress.avail_out;
    if (bytes_in) inputbuffer+=std::string(buf2,buf2+bytes_in);
    else break;
  }
}

// this was contributed by Marcelo E. Magallon <mmagallo@debian.org>
// under the GPL to glade-- in July, 2002
// adapted to work on sets

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
        i = in.find_first_not_of (delimiters, i);
        if (i == std::string::npos)
            return;   // nothing left but white space

        // find the end of the token
        std::string::size_type j = in.find_first_of (delimiters, i);

        // push token
        if (j == std::string::npos) {
            container.insert (in.substr(i));
            return;
        } else
            container.insert (in.substr(i, j-i));

        // set up for next loop
        i = j + 1;
    }
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
#if 0    
    const char *rsh=getenv("CVS_RSH");
    if (!rsh) rsh="rsh";
    newargv[newargc++]=rsh;
    if (!user.empty())
    { newargv[newargc++]="-l";
      newargv[newargc++]=user.c_str();
    }
    newargv[newargc++]=host.c_str();
    newargv[newargc++]="cvs server";
#else // try locally for testing
    newargv[newargc++]="cvs";
    newargv[newargc++]="server";
#endif    
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
  assert(answer.substr(0,15)=="Valid-requests ");
  // boost::tokenizer does not provide the needed functionality (preserve -)  
  stringtok(Valid_requests,answer.substr(15));
#if 0  
  for (stringset_t::const_iterator i=Valid_requests.begin();
         i!=Valid_requests.end();++i)
    std::cout << *i << ':';
#endif
  answer=readline();
  assert(answer=="ok");
  
  assert(Valid_requests.find("UseUnchanged")!=Valid_requests.end());

  writestr("UseUnchanged\n"); // ???
//  assert(readline()=="ok");
  ticker();

//  GzipStream(3);

//  writestr("Directory .\n");
//  do
//  { std::string answer=readline(readfd);
//    
//  }
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
//  error=inflateInit(&decompress); ??
//  if (error!=Z_OK) throw std::runtime_error("inflateInit"); ??
  gzip_level=level;
}

bool cvs_client::fetch_result(std::string &result)
{loop:
  std::string x=readline();
  if (x.substr(0,2)=="E ") 
  { std::cerr << x.substr(2) << '\n';
    goto loop;
  }
  if (x.substr(0,2)=="M ") 
  { result=x.substr(2);
    return true;
  }
  if (x=="ok") return false;
  std::cerr << "unrecognized result \"" << x << "\"\n";
  exit(1);
}

// CVS_RSH=rsh cvs -z0 -d localhost:/usr/local/cvsroot rlist -Red christof
const cvs_repository::tree_state_t &cvs_repository::now()
{ if (tree_states.empty())
  { // rlist -Red
    SendCommand("rlist","-e","-R","-d","--",module.c_str(),0);
    std::string result;
    std::list<std::string> l;
    while (fetch_result(result))
    { l.push_back(result);
    }
    ticker();
    for (std::list<std::string>::const_iterator i=l.begin();i!=l.end();++i)
      std::cerr << *i << ':';
  }
  return tree_states.back(); // wrong of course
}

#if 1
int main()
{ try
  { cvs_repository cl("localhost","/usr/local/cvsroot","","christof/java");
    const cvs_repository::tree_state_t &n=cl.now();
  } catch (std::exception &e)
  { std::cerr << e.what() << '\n';
  }
  return 0;
}
#endif
