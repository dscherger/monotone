// copyright (C) 2005 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <unistd.h>
#include <map>
#include <string>
#include <list>
#include <iostream>

class cvs_client
{ int readfd,writefd;
public:
  typedef std::map<std::string,std::string> cvsmanifest; // file,rev
private:
  // zusammen mit changelog, date, author(?)
  std::map<cvsmanifest*,cvsmanifest*> successor;

public:  
  cvs_client(const std::string &host, const std::string &root,
             const std::string &user=std::string(), 
             const std::string &module=std::string());
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

static void writestr(int fd, const std::string &s)
{ write(fd,s.c_str(),s.size());
}

// TODO: optimize
static std::string readline(int fd)
{ std::string &result;
  while (true)
  { char c;
    if (read(fd,&c,1)!=1) throw std::exception("read error");
    if (c=='\n') return result;
    result+=c;
  }
}

cvs_client::cvs_client(const std::string &host, const std::string &root, 
                    const std::string &user, const std::string &module)
    : readfd(-1), writefd(-1)
{ int fd1[2],fd2[2];
  pid_t child=pipe_and_fork(fd1,fd2);
  if (child<0) 
  {  throw std::exception("pipe/fork failed");
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
  std::cerr << answer << '\n';
  assert(answer.substr(0,2)=="M ");
  writestr(writefd,"UseUnchanged\n"); // ???
//  writestr(writefd,"Directory .\n");
//  do
//  { std::string answer=readline(readfd);
//    
//  }
}

const cvs_client::cvsmanifest &cvs_client::now()
{ return cvsmanifest();
}

#if 1
void main()
{ cvs_client cl("localhost","/usr/local/cvsroot","","christof");
  const cvsmanifest &n=cl.now();
}
#endif
