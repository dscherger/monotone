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
#include <boost/tokenizer.hpp>

class cvs_client
{ int readfd,writefd;
  size_t bytes_read,bytes_written;
  std::set<std::string> Valid_requests;
  
public:  
  cvs_client(const std::string &host, const std::string &root,
             const std::string &user=std::string(), 
             const std::string &module=std::string());
             
  void writestr(int fd, const std::string &s);
  std::string readline(int fd);
  
  size_t get_bytes_read() const { return bytes_read; }
  size_t get_bytes_written() const { return bytes_written; }
  void ticker()
  { std::cerr << "[bytes sent " << bytes_written << "] [bytes received "
      << bytes_read << "]\n";
  }
};

class cvs_repository : public cvs_client
{ 
public:
  typedef std::map<std::string,std::string> cvsmanifest; // file,rev
private:
  // zusammen mit changelog, date, author(?)
  std::map<cvsmanifest*,cvsmanifest*> successor;
public:  
  cvs_repository(const std::string &host, const std::string &root,
             const std::string &user=std::string(), 
             const std::string &module=std::string())
      : cvs_client(host,root,user,module) {}

  std::list<std::string> get_modules();
  void set_branch(const std::string &tag);
  const cvsmanifest &now();
  const cvsmanifest &find(const std::string &date,const std::string &changelog);
  const cvsmanifest &next(const cvsmanifest &m) const;
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

void cvs_client::writestr(int fd, const std::string &s)
{ bytes_written+=write(fd,s.c_str(),s.size());
}

// TODO: optimize
std::string cvs_client::readline(int fd)
{ std::string result;
  while (true)
  { char c;
    if (read(fd,&c,1)!=1) throw std::runtime_error("read error");
    ++bytes_read;
    if (c=='\n') return result;
    result+=c;
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
                    const std::string &user, const std::string &module)
    : readfd(-1), writefd(-1), bytes_read(0), bytes_written(0)
{ int fd1[2],fd2[2];
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
  
  writestr(writefd,"Root "+root+"\n");
  writestr(writefd,"Valid-responses ok error Valid-requests Checked-in "
              "New-entry Checksum Copy-file Updated Created Update-existing "
              "Merged Patched Rcs-diff Mode Mod-time Removed Remove-entry "
              "Set-static-directory Clear-static-directory Set-sticky "
              "Clear-sticky Template Clear-template Notified Module-expansion "
              "Wrapper-rcsOption M Mbinary E F MT\n");
  writestr(writefd,"valid-requests\n");
  std::string answer=readline(readfd);
//  std::cerr << answer << '\n';
  assert(answer.substr(0,15)=="Valid-requests ");
  stringtok(Valid_requests,answer.substr(15));
#if 0 // boost does not provide the needed functionality (preserve -)  
  typedef boost::tokenizer<char_delimiters_separator<char> > tokenizer;
  tokenizer tok(answer.substr(15));
  for(tokenizer::iterator i=tok.begin(); i!=tok.end();++i)
  { Valid_requests.insert(*i);
    std::cerr << *i << ' ';
  }
#endif  
  writestr(writefd,"UseUnchanged\n"); // ???
  ticker();
//  writestr(writefd,"Directory .\n");
//  do
//  { std::string answer=readline(readfd);
//    
//  }
}

const cvs_repository::cvsmanifest &cvs_repository::now()
{ static cvsmanifest res;
  return res;
}

#if 1
int main()
{ cvs_repository cl("localhost","/usr/local/cvsroot","","christof");
  const cvs_repository::cvsmanifest &n=cl.now();
  return 0;
}
#endif
