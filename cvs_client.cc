// copyright (C) 2005 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <list>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include "sanity.hh"
#include "cvs_client.hh"
#include <boost/lexical_cast.hpp>

void cvs_client::ticker(bool newline) const
{ std::cerr << "[bytes in: " << bytes_read << " out: " 
          << bytes_written << "]";
  if (newline) std::cerr << '\n';
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
{ if (s.size()) L(F("writestr(%s") % s); // s mostly contains the \n char
  if (!gzip_level)
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
    { throw oops("deflate error "+ boost::lexical_cast<std::string>(err));
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
    if (inputbuffer.empty()) throw oops("no data avail");
    std::string::size_type eol=inputbuffer.find('\n');
    if (eol==std::string::npos)
    { result+=inputbuffer;
      inputbuffer.clear();
    }
    else
    { result+=inputbuffer.substr(0,eol);
      inputbuffer.erase(0,eol+1);
      L(F("readline result '%s'\n") % result);
      return result;
    }
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
  fd_set rfds;
  
  FD_ZERO(&rfds);
  FD_SET(readfd, &rfds);
  if (select(readfd+1, &rfds, 0, 0, 0)!=1)
    throw oops("select error "+std::string(strerror(errno)));
  ssize_t avail_in=read(readfd,buf,sizeof buf);
  if (avail_in<1) 
    throw oops("read error "+std::string(strerror(errno)));
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
    { throw oops("inflate error "+boost::lexical_cast<std::string>(err));
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

bool cvs_client::begins_with(const std::string &s, const std::string &sub, unsigned &len)
{ if (s.substr(0,sub.size())==sub) { len=sub.size(); return true; }
  return false;
}

bool cvs_client::begins_with(const std::string &s, const std::string &sub)
{ return s.substr(0,sub.size())==sub;
}

cvs_client::cvs_client(const std::string &repository, const std::string &_module)
    : readfd(-1), writefd(-1), bytes_read(), bytes_written(),
      gzip_level(), module(_module)
{ bool pserver=false;
  std::string user;
  { unsigned len;
    std::string d_arg=repository;
    if (begins_with(d_arg,":pserver:",len))
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
    std::string::size_type root_start=colon;
    if (colon!=std::string::npos) 
    { host=d_arg.substr(host_start,colon-host_start); 
      ++root_start; 
    }
    else root_start=0;
    root=d_arg.substr(root_start);
  }
  rcs_root=root;

  memset(&compress,0,sizeof compress);
  memset(&decompress,0,sizeof decompress);
  if (pserver)
  { // it looks like I run into the same problems on Win32 again and again:
    //  pipes and sockets, so this is not portable except by using the
    //  Netxx::PipeStream from the ssh branch ... postponed
    static const int pserver_port=2401;
    writefd = socket(PF_INET, SOCK_STREAM, 0);
    I(writefd>=0);
    struct hostent *ptHost = gethostbyname(host.c_str());
    if (!ptHost)
    { L(F("Can't find address for host %s\n") % host);
      throw oops("gethostbyname " + host + " failed");
    }
    struct sockaddr_in tAddr;
    tAddr.sin_family = AF_INET;
    tAddr.sin_port = htons(pserver_port);
    tAddr.sin_addr = *(struct in_addr *)(ptHost->h_addr);
    if (connect(writefd, (struct sockaddr*)&tAddr, sizeof(tAddr)))
    { L(F("Can't connect to port %d on %s\n") % pserver_port % host);
      throw oops("connect to port "+boost::lexical_cast<std::string>(pserver_port)
              +" on "+host+" failed");
    }
    readfd=writefd;
    fcntl(readfd,F_SETFL,fcntl(readfd,F_GETFL)|O_NONBLOCK);
    writestr("BEGIN AUTH REQUEST\n");
    writestr(root+"\n");
    writestr(user+"\n");
    writestr(pserver_password(":pserver:"+user+"@"+host+":"+root)+"\n");
    writestr("END AUTH REQUEST\n");
    std::string answer=readline();
    if (answer!="I LOVE YOU")
    { L(F("pserver Authentification failed\n"));
      throw oops("pserver auth failed: "+answer);
    }
  }
  else // rsh
  { int fd1[2],fd2[2];
    pid_t child=pipe_and_fork(fd1,fd2);
    if (child<0) 
    {  throw oops("pipe/fork failed "+std::string(strerror(errno)));
    }
    else if (!child)
    { const unsigned newsize=64;
      const char *newargv[newsize];
      unsigned newargc=0;
      if (host.empty())
      { 
#if 1
        newargv[newargc++]="cvs";
        newargv[newargc++]="server";
#else // DEBUGGING
        newargv[newargc++]="sh";
        newargv[newargc++]="-c";
        newargv[newargc++]="tee cvs_server.log | cvs server";
#endif

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
    fcntl(readfd,F_SETFL,fcntl(readfd,F_GETFL)|O_NONBLOCK);
    
    if (host.empty())
    {
      // set host name for author qualification
      char domainname[1024];
      *domainname=0;
      if (gethostname(domainname,sizeof domainname))
        throw oops("gethostname "+std::string(strerror(errno)));
      domainname[sizeof(domainname)-1]=0;
      unsigned len=strlen(domainname);
      if (len && len<sizeof(domainname)-2)
      { domainname[len]='.';
        domainname[++len]=0;
      }
      if (getdomainname(domainname+len,sizeof(domainname)-len))
        throw oops("getdomainname "+std::string(strerror(errno)));
      domainname[sizeof(domainname)-1]=0;
      host=domainname;
      L(F("hostname %s\n") % host);
    }
  }
  
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
  if (error!=Z_OK) throw oops("deflateInit "+boost::lexical_cast<std::string>(error));
  error=inflateInit(&decompress);
  if (error!=Z_OK) throw oops("inflateInit "+boost::lexical_cast<std::string>(error));
}

void cvs_client::GzipStream(int level)
{ if (!CommandValid("Gzip-stream")) return;
  std::string cmd="Gzip-stream ";
  cmd+=char('0'+level);
  cmd+='\n';
  writestr(cmd);
  int error=deflateParams(&compress,level,Z_DEFAULT_STRATEGY);
  if (error!=Z_OK) throw oops("deflateParams "+boost::lexical_cast<std::string>(error));
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
      || begins_with(x,"Set-static-directory ",len)
      || begins_with(x,"Removed ",len)
      || begins_with(x,"Remove-entry",len))
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
  if (begins_with(x,"Copy-file ",len))
  { result.push_back(std::make_pair("CMD",x.substr(0,len-1)));
    result.push_back(std::make_pair("dir",x.substr(len)));
    result.push_back(std::make_pair("file",readline()));
    result.push_back(std::make_pair("new-file",readline()));
    return true;
  }
  if (begins_with(x,"Checksum ",len))
  { result.push_back(std::make_pair("CMD",x.substr(0,len-1)));
    result.push_back(std::make_pair("data",x.substr(len)));
    return true;
  }
  if (begins_with(x,"Checked-in ",len))
  { result.push_back(std::make_pair("CMD",x.substr(0,len-1)));
    result.push_back(std::make_pair("dir",x.substr(len)));
    result.push_back(std::make_pair("rcs",readline()));
    result.push_back(std::make_pair("new entries line",readline()));
    return true;
  }
  if (begins_with(x,"Created ",len) || begins_with(x,"Update-existing ",len)
      || begins_with(x,"Rcs-diff ",len) || begins_with(x,"Merged ",len))
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
  if (x=="error  ")
  { result.push_back(std::make_pair("CMD",x));
    return true;
  }
  
error:
  std::cerr << "unrecognized response \"" << x << "\"\n";
  exit(1);
}

static time_t cvs111date2time_t(const std::string &t)
{ // 2000/11/10 14:43:25
  I(t.size()==19);
  I(t[4]=='/' && t[7]=='/');
  I(t[10]==' ' && t[13]==':');
  I(t[16]==':');
  struct tm tm;
  memset(&tm,0,sizeof tm);
  tm.tm_year=atoi(t.substr(0,4).c_str())-1900;
  tm.tm_mon=atoi(t.substr(5,2).c_str())-1;
  tm.tm_mday=atoi(t.substr(8,2).c_str());
  tm.tm_hour=atoi(t.substr(11,2).c_str());
  tm.tm_min=atoi(t.substr(14,2).c_str());
  tm.tm_sec=atoi(t.substr(17,2).c_str());
  time_t result=-1;
  result=mktime(&tm); // I _assume_ this is local time :-(
  return result;
}

static time_t rls_l2time_t(const std::string &t)
{ // 2003-11-26 09:20:57 +0000
  I(t.size()==25);
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

// third format:
// 19 Nov 1996 11:22:50 -0000
// 4 Jun 1999 12:00:41 -0000
// 19 Jul 2002 07:33:26 -0000

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
          
          if (keyword=="----") keyword.clear();
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
        { file=rcs_file2path(result.substr(len));
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
        { state=st_desc;
          description.clear();
        }
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
        unsigned len=0;
        I(begins_with(result,"revision ",len));
        revision=result.substr(len);
        state=st_date_author;
        break;
      }
      case st_date_author:
      { if (lresult.size()==1) // M ... (cvs 1.11.1p1)
        { std::string result=combine_result(lresult);
          unsigned len=0;
          I(begins_with(result,"date: ",len));
          std::string::size_type authorpos=result.find(";  author: ",len);
          I(authorpos!=std::string::npos);
          std::string::size_type authorbegin=authorpos+11;
          std::string::size_type statepos=result.find(";  state: ",authorbegin);
          I(statepos!=std::string::npos);
          std::string::size_type statebegin=statepos+10;
          std::string::size_type linespos=result.find(";",statebegin);
          // ";  lines: "
          I(linespos!=std::string::npos);
          checkin_time=cvs111date2time_t(result.substr(len,authorpos-len));
          author=result.substr(authorbegin,statepos-authorbegin);
          dead=result.substr(statebegin,linespos-statebegin);
        }
        else // MT ... (cvs 1.12.9)
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
        }
        state=st_msg;
        message.clear();
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

struct checkout cvs_client::CheckOut(const std::string &file, const std::string &revision)
{ struct checkout result;
  writestr("Directory .\n"+root+"\n");
  SendCommand("co",/*"-N","-P",*/"-r",revision.c_str(),"--",file.c_str(),0);
  enum { st_co
       } state=st_co;
  std::vector<std::pair<std::string,std::string> > lresult;
  std::string dir,dir2,rcsfile;
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
          else if (lresult[0].second=="Remove-entry"
              || lresult[0].second=="Removed")
          { I(lresult.size()==3);
            result.dead=true;
          }
          else if (lresult[0].second=="Mod-time")
          { I(lresult.size()==2);
            I(lresult[1].first=="date");
            // this is 18 Nov 1996 14:39:40 -0000 format - strange ...
            // result.mod_time=xyz2time_t(lresult[1].second);
          }
          else if (lresult[0].second=="Created"
              || lresult[0].second=="Update-existing")
          // Update-existing results after crossing a dead state
          { // std::cerr << combine_result(lresult) << '\n';
            I(lresult.size()==7);
            I(lresult[6].first=="data");
//            result.mode=?
            result.contents=lresult[6].second;
            L(F("file %s revision %s: %d bytes\n") % file 
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
  return result;
}

static std::string basename(const std::string &s)
{ std::string::size_type lastslash=s.rfind("/");
  if (lastslash==std::string::npos) return s;
  return s.substr(lastslash+1);
}

static std::string dirname(const std::string &s)
{ std::string::size_type lastslash=s.rfind("/");
  if (lastslash==std::string::npos) return ".";
  if (!lastslash) return "/";
  return s.substr(0,lastslash);
}

struct cvs_client::update cvs_client::Update(const std::string &file, 
            const std::string &old_revision, const std::string &new_revision)
{ struct update result;
  writestr("Directory .\n"+root+"/"+dirname(file)+"\n");
  std::string bname=basename(file);
  writestr("Entry /"+bname+"/"+old_revision+"///\n");
  writestr("Unchanged "+bname+"\n");
  SendCommand("update","-r",new_revision.c_str(),"-u","--",bname.c_str(),0);
  std::vector<std::pair<std::string,std::string> > lresult;
  std::string dir,dir2,rcsfile;
  enum { st_normal, st_merge } state=st_normal;
  while (fetch_result(lresult))
  { I(!lresult.empty());
    unsigned len=0;
    if (lresult[0].first=="CMD")
    { if (lresult[0].second=="Update-existing")
      { I(lresult.size()==7);
        I(lresult[6].first=="data");
        dir=lresult[1].second;
        result.contents=lresult[6].second;
      }
      else if (lresult[0].second=="Rcs-diff")
      { I(lresult.size()==7);
        I(lresult[6].first=="data");
        dir=lresult[1].second;
        result.patch=lresult[6].second;
      }
      else if (lresult[0].second=="Checksum")
      { I(lresult.size()==2);
        I(lresult[1].first=="data");
        result.checksum=lresult[1].second;
      }
      else if (lresult[0].second=="Removed")
      { I(lresult.size()==3);
        result.removed=true;
      }
      else if (lresult[0].second=="Copy-file")
      { I(state==st_merge);
      }
      else if (lresult[0].second=="Merged")
      { I(state==st_merge);
      }
      else if (lresult[0].second=="error  ")
      { I(state==st_merge);
        break;
      }
      else
      { std::cerr << "unrecognized response " << lresult[0].second << '\n';
      }
    }
    else if (lresult[0].second=="+updated")
    { // std::cerr << combine_result(lresult) << '\n';
    }
    else if (lresult[0].second=="P ")
    { // std::cerr << combine_result(lresult) << '\n';
      I(lresult.size()==2);
      I(lresult[1].first=="fname");
    }
    else if (lresult[0].second=="M ")
    { I(lresult.size()==2);
      I(lresult[1].first=="fname");
      state=st_merge;
    }
    else if (begins_with(lresult[0].second,"RCS file: ",len))
    { I(state==st_normal);
      state=st_merge;
    }
    else if (begins_with(lresult[0].second,"retrieving revision ",len))
    { I(state==st_merge);
    }
    else if (begins_with(lresult[0].second,"Merging ",len))
    { I(state==st_merge);
    }
    else if (begins_with(lresult[0].second,"C ",len))
    { state=st_merge;
      I(lresult.size()==2);
      I(lresult[1].first=="fname");
    }
    else 
    { std::cerr << "unrecognized response " << lresult[0].second << '\n';
    }
  }
  if (state==st_merge)
  { W(F("Update %s->%s of %s exposed CVS bug\n") % old_revision % new_revision % file);
    checkout result2=CheckOut(file,new_revision);
    result.contents=result2.contents;
    result.patch=std::string();
    result.checksum=std::string();
    result.removed=result2.dead;
  }
  return result;
}

std::string cvs_client::pserver_password(const std::string &root)
{ const char *home=getenv("HOME");
  if (!home) home="";
  std::ifstream fs((std::string(home)+"/.cvspass").c_str());
  while (fs.good())
  { char buf[1024];
    if (fs.getline(buf,sizeof buf).good())
    { std::string line=buf;
      if (line.substr(0,3)=="/1 ") line.erase(0,3);
      if (line.size()>=root.size()+2 && line.substr(0,root.size())==root
          && line[root.size()]==' ')
        return line.substr(root.size()+1);
    }
  }
  return "A"; // empty password
}

std::string cvs_client::shorten_path(const std::string &p) const
{ unsigned len=0;
  if (cvs_client::begins_with(p,module,len))
  { if (p[len]=='/') ++len;
    return p.substr(len);
  }
  return p;
}

std::string cvs_client::rcs_file2path(std::string file) const
{ // try to guess a sane file name (e.g. on cvs.gnome.org)
  if (file.substr(0,rcs_root.size())!=rcs_root)
  { std::string::size_type modpos=file.find(module);
    if (modpos!=std::string::npos) // else ... who knows where to put it
    { I(modpos>=1);
      I(file[modpos-1]=='/');
      std::string oldroot=rcs_root;
      const_cast<std::string&>(rcs_root)=file.substr(0,modpos-1);
      W(F("changing rcs root dir from %s to %s\n") % oldroot % rcs_root);
      file.erase(0,rcs_root.size());
    }
  }
  else file.erase(0,rcs_root.size());
  I(file[0]=='/');
  file.erase(0,1);
  if (file.substr(file.size()-2)==",v") file.erase(file.size()-2,2);
  std::string::size_type lastslash=file.rfind('/');
  if (lastslash!=std::string::npos && lastslash>=5
          && file.substr(lastslash-5,6)=="Attic/")
    file.erase(lastslash-5,6);
  return file;
}
