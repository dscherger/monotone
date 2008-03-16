// copyright (C) 2005 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <list>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <base.hh>
#include "sanity.hh"
#include "cvs_client.hh"
#include <boost/lexical_cast.hpp>
#include <netxx/stream.h>
#include "stringtok.hh"
#include <boost/format.hpp>

void cvs_client::writestr(const std::string &s, bool flush)
{ if (s.size()) L(FL("writestr(%s") % s); // s mostly contains the \n char
  if (!gzip_level)
  { if (s.size() && byte_out_ticker.get())
      (*byte_out_ticker)+=stream->write(s.c_str(),s.size());
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
    E(err==Z_OK || err==Z_BUF_ERROR, F("deflate error %d") % err);
    unsigned written=sizeof(outbuf)-compress.avail_out;
    if (written && byte_out_ticker.get())
      (*byte_out_ticker)+=stream->write(outbuf,written);
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
    E(!inputbuffer.empty(),F("no data avail"));
    std::string::size_type eol=inputbuffer.find('\n');
    if (eol==std::string::npos)
    { result+=inputbuffer;
      inputbuffer.clear();
    }
    else
    { result+=inputbuffer.substr(0,eol);
      inputbuffer.erase(0,eol+1);
      L(FL("readline result '%s'\n") % result);
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

void cvs_client::underflow()
{ char buf[1024],buf2[1024];
  Netxx::PipeCompatibleProbe probe;
  probe.add(*stream, Netxx::Probe::ready_read);
try_again:
  Netxx::Probe::result_type res = probe.ready(Netxx::Timeout(60L),
                      Netxx::Probe::ready_read); // 60 seconds
  E((res.second&Netxx::Probe::ready_read),F("timeout reading from CVS server"));
  ssize_t avail_in=stream->read(buf,sizeof buf);
  E(avail_in>0, F("read error %s") % strerror(errno));
  if (byte_in_ticker.get())
    (*byte_in_ticker)+=avail_in;
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
    E(err==Z_OK || err==Z_BUF_ERROR, F("inflate error %d") % err);
    unsigned bytes_in=sizeof(buf2)-decompress.avail_out;
    if (bytes_in) inputbuffer+=std::string(buf2,buf2+bytes_in);
    else break;
  }
  if (inputbuffer.empty()) goto try_again;
}

static std::string trim(const std::string &s)
{ std::string::size_type start=s.find_first_not_of(" ");
  if (start==std::string::npos) return std::string();
  std::string::size_type end=s.find_last_not_of(" ");
  if (end==std::string::npos) end=s.size();
  else ++end;
  // "  AA  " gives start=2 end=3, so substr(2,1) is correct
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

void cvs_client::SendCommand(std::string const& cmd, std::vector<std::string> const&args)
{ for (std::vector<std::string>::const_iterator i=args.begin();i!=args.end();++i)
    writestr("Argument "+*i+"\n");
  writestr(cmd+"\n");
}

bool cvs_client::begins_with(const std::string &s, const std::string &sub, unsigned &len)
{ std::string::size_type sublen=sub.size();
  if (s.size()<sublen) return false;
  if (!s.compare(0,sublen,sub)) { len=sublen; return true; }
  return false;
}

bool cvs_client::begins_with(const std::string &s, const std::string &sub)
{ std::string::size_type len=sub.size();
  if (s.size()<len) return false;
  return !s.compare(0,len,sub);
}

cvs_client::cvs_client(const std::string &repository, const std::string &_module,
        std::string const &_branch, bool do_connect)
    : byte_in_ticker(), byte_out_ticker(),
      gzip_level(), pserver(), module(_module), branch(_branch)
{ // parse the arguments
  { unsigned len;
    std::string d_arg=repository;
    if (begins_with(d_arg,":ext:",len)) d_arg.erase(0,len);
    else if (begins_with(d_arg,":pserver:",len))
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

  memset(&compress,0,sizeof compress);
  memset(&decompress,0,sizeof decompress);

  if (do_connect) connect();
  else if (!pserver && host.empty()) host=localhost_name();
}

std::string cvs_client::localhost_name()
{ // get localhost's name
  char domainname[1024];
  *domainname=0;
#ifdef WIN32
  strcpy(domainname,"localhost"); // gethostname does not work here ...
#else
  E(!gethostname(domainname,sizeof domainname),
    F("gethostname %s\n") % strerror(errno));
  domainname[sizeof(domainname)-1]=0;
#endif
#if !defined(__sun) && !defined(WIN32)
  unsigned len=strlen(domainname);
  if (len && len<sizeof(domainname)-2)
  { domainname[len]='.';
    domainname[++len]=0;
  }
  E(!getdomainname(domainname+len,sizeof(domainname)-len),
    F("getdomainname %s\n") % strerror(errno));
  domainname[sizeof(domainname)-1]=0;
#endif
  L(FL("localhost's name %s\n") % domainname);
  return domainname;
}

void cvs_client::connect()
{ byte_in_ticker.reset(new ticker("bytes in", ">", 256));
  byte_out_ticker.reset(new ticker("bytes out", "<", 256));

  memset(&compress,0,sizeof compress);
  memset(&decompress,0,sizeof decompress);

  if (pserver)
  { Netxx::Address addr(host.c_str(), 2401);
    stream=boost::shared_ptr<Netxx::StreamBase>(new Netxx::Stream(addr, Netxx::Timeout(30)));
    
    writestr("BEGIN AUTH REQUEST\n");
    writestr(root+"\n");
    writestr(user+"\n");
    writestr(pserver_password(":pserver:"+user+"@"+host+":"+root)+"\n");
    writestr("END AUTH REQUEST\n");
    std::string answer=readline();
    E(answer=="I LOVE YOU", F("pserver Authentification failed\n"));
  }
  else // rsh
  { std::string local_name=localhost_name();

    if (host==local_name) host="";
    
    std::string cmd;
    std::vector<std::string> args;
    if (host.empty())
    { const char *cvs_client_log=getenv("CVS_CLIENT_LOG");
      if (!cvs_client_log)
      { cmd="cvs";
        args.push_back("server");
      }
      else // ugly hack :-(
      { cmd="sh";
        args.push_back("-c");
        args.push_back(std::string("tee \"")+cvs_client_log+".in"+
            "\" | cvs server | tee \""+cvs_client_log+".out\"");
      }
    }
    else
    { const char *rsh=getenv("CVS_RSH");
      if (!rsh) rsh="rsh";
      cmd=rsh;
      if (!user.empty())
      { args.push_back("-l");
        args.push_back(user);
      }
      args.push_back(host);
      args.push_back("cvs server");
    }
    if (host.empty()) host=local_name;
    L(FL("spawning pipe to '%s' ") % cmd);
    for (std::vector<std::string>::const_iterator i=args.begin();i!=args.end();++i)
      L(FL("'%s' ") % *i);
    L(FL("\n"));
    stream=boost::shared_ptr<Netxx::StreamBase>(new Netxx::PipeStream(cmd,args));
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
  MM(answer);
  E(begins_with(answer,"Valid-requests "),
      F("CVS server answered '%s' to Valid-requests\n") % answer);
  // boost::tokenizer does not provide the needed functionality (e.g. preserve -)
  push_back2insert<stringset_t> adaptor(Valid_requests);
  stringtok(adaptor,answer.substr(15));
  answer=readline();
  E(answer=="ok",
      F("CVS server did not answer ok to Valid-requests: %s\n") % answer);
  
  I(CommandValid("UseUnchanged"));
  writestr("UseUnchanged\n");

  writestr("Global_option -q\n"); // -Q?
}

void cvs_client::drop_connection()
{ byte_in_ticker.reset();
  byte_out_ticker.reset();
  deflateEnd(&compress);
  inflateEnd(&decompress);
  gzip_level=0;
  stream.reset();
}

cvs_client::~cvs_client()
{ drop_connection();
}

void cvs_client::reconnect()
{ drop_connection();
  connect();
}

void cvs_client::InitZipStream(int level)
{ int error=deflateInit(&compress,level);
  E(error==Z_OK,F("deflateInit %d\n") % error);
  error=inflateInit(&decompress);
  E(error==Z_OK,F("inflateInit %d\n") % error);
}

void cvs_client::GzipStream(int level)
{ if (!CommandValid("Gzip-stream")) return;
  std::string cmd="Gzip-stream ";
  cmd+=char('0'+level);
  cmd+='\n';
  writestr(cmd);
  int error=deflateParams(&compress,level,Z_DEFAULT_STRATEGY);
  E(error==Z_OK,F("deflateParams %d\n") % error);
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
  MM(x);
  unsigned len=0;
  if (x=="F" || x=="F ")
  { // flush stderr
    goto loop;
  }
  if (x.size()<2) goto error;
  if (begins_with(x,"E ",len)) 
  { W(F("%s\n") % x.substr(len));
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
      || begins_with(x,"Clear-static-directory ",len)
      || begins_with(x,"Clear-template ",len)
      || begins_with(x,"Removed ",len)
      || begins_with(x,"Remove-entry ",len))
  { result.push_back(std::make_pair("CMD",x.substr(0,len-1)));
    result.push_back(std::make_pair("dir",x.substr(len)));
    result.push_back(std::make_pair("rcs",readline()));
    return true;
  }
  if (begins_with(x,"Template ",len))
  { result.push_back(std::make_pair("CMD",x.substr(0,len-1)));
    result.push_back(std::make_pair("dir",x.substr(len)));
    result.push_back(std::make_pair("path",readline()));
    std::string length=readline();
    result.push_back(std::make_pair("length",length));
    result.push_back(std::make_pair("data",read_n(boost::lexical_cast<long>(length.c_str()))));
    return true;
  }
  if (begins_with(x,"Mod-time ",len))
  { result.push_back(std::make_pair("CMD",x.substr(0,len-1)));
    result.push_back(std::make_pair("date",x.substr(len)));
    return true;
  }
  if (begins_with(x,"Mode ",len))
  { result.push_back(std::make_pair("CMD",x.substr(0,len-1)));
    result.push_back(std::make_pair("mode",x.substr(len)));
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
  if (begins_with(x,"Module-expansion ",len))
  { result.push_back(std::make_pair("CMD",x.substr(0,len-1)));
    result.push_back(std::make_pair("dir",x.substr(len)));
    return true;
  }
  if (begins_with(x,"Checked-in ",len))
  { result.push_back(std::make_pair("CMD",x.substr(0,len-1)));
    result.push_back(std::make_pair("dir",x.substr(len)));
    result.push_back(std::make_pair("rcs",readline()));
    result.push_back(std::make_pair("new entries line",readline()));
    return true;
  }
  if (begins_with(x,"Set-sticky ",len))
  { result.push_back(std::make_pair("CMD",x.substr(0,len-1)));
    result.push_back(std::make_pair("dir",x.substr(len)));
    result.push_back(std::make_pair("rcs",readline()));
    result.push_back(std::make_pair("tag",readline()));
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
    result.push_back(std::make_pair("data",read_n(boost::lexical_cast<long>(length.c_str()))));
    return true;
  }
  if (x=="Mbinary ")
  { result.push_back(std::make_pair("CMD",x.substr(0,len-1)));
    std::string length=readline();
    result.push_back(std::make_pair("length",length));
    result.push_back(std::make_pair("data",read_n(boost::lexical_cast<long>(length.c_str()))));
    return true;
  }
  if (x=="error  ")
  { result.push_back(std::make_pair("CMD","error"));
    return true;
  }
  
error:
  W(F("unhandled server response \"%s\"\n") % x);
  exit(1);
}

/* Converts struct tm to time_t, assuming the data in tm is UTC rather
   than local timezone.

   mktime is similar but assumes struct tm, also known as the
   "broken-down" form of time, is in local time zone.  mktime_from_utc
   uses mktime to make the conversion understanding that an offset
   will be introduced by the local time assumption.

   mktime_from_utc then measures the introduced offset by applying
   gmtime to the initial result and applying mktime to the resulting
   "broken-down" form.  The difference between the two mktime results
   is the measured offset which is then subtracted from the initial
   mktime result to yield a calendar time which is the value returned.

   tm_isdst in struct tm is set to 0 to force mktime to introduce a
   consistent offset (the non DST offset) since tm and tm+o might be
   on opposite sides of a DST change.

   Some implementations of mktime return -1 for the nonexistent
   localtime hour at the beginning of DST.  In this event, use
   mktime(tm - 1hr) + 3600.

   Schematically
     mktime(tm)   --> t+o
     gmtime(t+o)  --> tm+o
     mktime(tm+o) --> t+2o
     t+o - (t+2o - t+o) = t

   Note that glibc contains a function of the same purpose named
   `timegm' (reverse of gmtime).  But obviously, it is not universally
   available, and unfortunately it is not straightforwardly
   extractable for use here.  Perhaps configure should detect timegm
   and use it where available.

   Contributed by Roger Beeman <beeman@cisco.com>, with the help of
   Mark Baushke <mdb@cisco.com> and the rest of the Gurus at CISCO.
   Further improved by Roger with assistance from Edward J. Sabol
   based on input by Jamie Zawinski.
   
   Pointed out by Nathaniel Smith on the monotone mailing list.  */

static time_t
mktime_from_utc (struct tm *t)
{
  time_t tl, tb;
  struct tm *tg;

  tl = mktime (t);
  if (tl == -1)
    {
      t->tm_hour--;
      tl = mktime (t);
      I(tl != -1);
      tl += 3600;
    }
  tg = gmtime (&tl);
  tg->tm_isdst = 0;
  tb = mktime (tg);
  if (tb == -1)
    {
      tg->tm_hour--;
      tb = mktime (tg);
      I(tb != -1);
      tb += 3600;
    }
  return (tl - (tb - tl));
}

static time_t timezone2time_t(const struct tm &tm, int offset_min)
{ I(!offset_min);
  struct tm copy=tm;
  return mktime_from_utc(&copy);
}

static time_t cvs111date2time_t(const std::string &t)
{ // 2000/11/10 14:43:25
  MM(t);
  E(t.size()==19, F("cvs111date2time_t unknown format '%s'\n") % t);
  I(t[4]=='/' && t[7]=='/');
  I(t[10]==' ' && t[13]==':');
  I(t[16]==':');
  struct tm tm;
  memset(&tm,0,sizeof tm);
  tm.tm_year=boost::lexical_cast<int>(t.substr(0,4).c_str())-1900;
  tm.tm_mon=boost::lexical_cast<int>(t.substr(5,2).c_str())-1;
  tm.tm_mday=boost::lexical_cast<int>(t.substr(8,2).c_str());
  tm.tm_hour=boost::lexical_cast<int>(t.substr(11,2).c_str());
  tm.tm_min=boost::lexical_cast<int>(t.substr(14,2).c_str());
  tm.tm_sec=boost::lexical_cast<int>(t.substr(17,2).c_str());
  // on my debian/woody server (1.11) this is UTC ...
  return timezone2time_t(tm,0); 
}

static time_t rls_l2time_t(const std::string &t)
{ // 2003-11-26 09:20:57 +0000
  MM(t);
  E(t.size()==25, F("rls_l2time_t unknown format '%s'\n") % t);
  I(t[4]=='-' && t[7]=='-');
  I(t[10]==' ' && t[13]==':');
  I(t[16]==':' && t[19]==' ');
  I(t[20]=='+' || t[20]=='-');
  struct tm tm;
  memset(&tm,0,sizeof tm);
  tm.tm_year=boost::lexical_cast<int>(t.substr(0,4).c_str())-1900;
  tm.tm_mon=boost::lexical_cast<int>(t.substr(5,2).c_str())-1;
  tm.tm_mday=boost::lexical_cast<int>(t.substr(8,2).c_str());
  tm.tm_hour=boost::lexical_cast<int>(t.substr(11,2).c_str());
  tm.tm_min=boost::lexical_cast<int>(t.substr(14,2).c_str());
  tm.tm_sec=boost::lexical_cast<int>(t.substr(17,2).c_str());
  int dst_offs=boost::lexical_cast<int>(t.substr(20,5).c_str());
//  L(FL("%d-%d-%d %d:%02d:%02d %04d") % tm.tm_year % tm.tm_mon % tm.tm_mday 
//    % tm.tm_hour % tm.tm_min % tm.tm_sec % dst_offs );
  tm.tm_isdst=0;
  return timezone2time_t(tm,dst_offs);
}

// third format:
// 19 Nov 1996 11:22:50 -0000
// 4 Jun 1999 12:00:41 -0000
// 19 Jul 2002 07:33:26 -0000

// Jan,Feb,Mar,Apr,May,Jun,Jul,Aug,Sep,Oct,Nov,Dec
// Apr,Aug,Dec,Feb,Jan,Jul,Jun,Mar,May,Nov,Oct,Sep
static time_t monname2month(const std::string &x)
{ MM(x);
  I(x.size()==3);
  // I hope this will never get internationalized
  if (x[0]=='O') return 10;
  if (x[0]=='S') return 9;
  if (x[0]=='D') return 12;
  if (x[0]=='F') return 2;
  if (x[0]=='N') return 11;
  if (x[0]=='A') return x[1]=='p'?4:8;
  if (x[0]=='M') return x[2]=='r'?3:5;
  I(x[0]=='J');
  return x[1]=='a'?1:(x[2]=='n'?6:7);
  return 0;
}

static time_t mod_time2time_t(const std::string &t)
{ std::vector<std::string> parts;
  MM(t);
  stringtok(parts,t);
  I(parts.size()==5);
  struct tm tm;
  memset(&tm,0,sizeof tm);
  I(parts[3][2]==':' && parts[3][5]==':');
  I(parts[4][0]=='+' || parts[4][0]=='-');
  tm.tm_year=boost::lexical_cast<int>(parts[2].c_str())-1900;
  tm.tm_mon=monname2month(parts[1])-1;
  tm.tm_mday=boost::lexical_cast<int>(parts[0].c_str());
  tm.tm_hour=boost::lexical_cast<int>(parts[3].substr(0,2).c_str());
  tm.tm_min=boost::lexical_cast<int>(parts[3].substr(3,2).c_str());
  tm.tm_sec=boost::lexical_cast<int>(parts[3].substr(6,2).c_str());
  int dst_offs=boost::lexical_cast<int>(parts[4].c_str());
  tm.tm_isdst=0;
  return timezone2time_t(tm,dst_offs);
}

time_t cvs_client::Entries2time_t(const std::string &t)
{ MM(t);
  E(t.size()==24, F("Entries2time_t unknown format '%s'\n") % t);
  I(t[3]==' ');
  I(t[7]==' ');
  std::vector<std::string> parts;
  stringtok(parts,t);
  // stringtok is not overly well suited for this task, every single whitespace
  // separates parts
  if (parts.size()==6 && t[8]==' ' && parts[2].empty())
    parts.erase(parts.begin()+2);
  I(parts.size()==5); //  || parts.size()==6);
  struct tm tm;
  memset(&tm,0,sizeof tm);
  I(parts[3][2]==':' && parts[3][5]==':');
  tm.tm_year=boost::lexical_cast<int>(parts[4].c_str())-1900;
  tm.tm_mon=monname2month(parts[1])-1;
  tm.tm_mday=boost::lexical_cast<int>(parts[2].c_str());
  tm.tm_hour=boost::lexical_cast<int>(parts[3].substr(0,2).c_str());
  tm.tm_min=boost::lexical_cast<int>(parts[3].substr(3,2).c_str());
  tm.tm_sec=boost::lexical_cast<int>(parts[3].substr(6,2).c_str());
  tm.tm_isdst=0;
  // at least for me it was UTC ...
  return timezone2time_t(tm,0);
}

std::string cvs_client::time_t2rfc822(time_t t)
{ static const char * const months[12] = 
  {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  struct tm *tm=gmtime(&t);
  I(tm);
  // do _not_ translate this into locale format (e.g. F() )
  return (boost::format("%02d %s %d %02d:%02d:%02d +0000") 
    % tm->tm_mday % months[tm->tm_mon] % (tm->tm_year+1900)
    % tm->tm_hour % tm->tm_min % tm->tm_sec).str();
}

void cvs_client::Directory(const std::string &path)
{ MM(path);
  if (path.empty() || path==".") // ???
  { std::map<std::string,std::string>::const_iterator i=server_dir.find("");
    I(i!=server_dir.end());
    writestr("Directory .\n"+i->second+"\n");
  }
  else
  { std::map<std::string,std::string>::reverse_iterator i;
    std::string path_with_slash=path+"/";
    unsigned len=0;
    for (i=server_dir.rbegin();i!=server_dir.rend();++i)
    { if (begins_with(path_with_slash,i->first,len)) break;
    }
    I(!server_dir.empty());
//    if (i==server_dir.rend()) { --i; len=0; } // take the last one
    I(i!=server_dir.rend());
//    if (path[len]=='/') ++len;
    I(!i->second.empty());
    I(i->second[i->second.size()-1]=='/');
    std::string rcspath=i->second;
    if (len<path.size()) rcspath+=path_with_slash.substr(len);
    writestr("Directory "+path+"\n"+rcspath+"\n");
  }
}

void cvs_client::RList(const rlist_callbacks &cb,bool dummy,...)
{ primeModules();
  { va_list ap;
    va_start(ap,dummy);
    SendCommand("rlist",ap);
    va_end(ap);
  }
  std::vector<std::pair<std::string,std::string> > lresult;
  enum { st_dir, st_file } state=st_dir;
  std::string directory;
  while (fetch_result(lresult))
  { L(FL("result %s\n") % combine_result(lresult));
    switch(state)
    { case st_dir:
      { std::string result=combine_result(lresult);
        I(result.size()>=2);
        I(result[result.size()-1]==':');
        directory=result.substr(0,result.size()-1);
        state=st_file;
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

void cvs_client::Log_internal(const rlog_callbacks &cb,const std::string &file,va_list ap)
{ Directory(dirname(std::string(file)));
  std::string bname=basename(std::string(file));
  writestr("Entry /"+bname+"/1.1.1.1//-kb/\n");
  writestr("Unchanged "+bname+"\n");
  { const char *arg;
    while ((arg=va_arg(ap,const char *)))
    { writestr("Argument "+std::string(arg)+"\n");
    }
  }
  writestr("Argument --\n"
        "Argument "+bname+"\n"
        "log\n");
  processLogOutput(cb);
}

void cvs_client::Log_internal(const rlog_callbacks &cb,const std::string &file,
                              std::vector<std::string> const &args)
{ Directory(dirname(std::string(file)));
  std::string bname=basename(std::string(file));
  writestr("Entry /"+bname+"/1.1.1.1//-kb/\n");
  writestr("Unchanged "+bname+"\n");
  for (std::vector<std::string>::const_iterator i=args.begin();i!=args.end();++i)
    writestr("Argument "+*i+"\n");
  writestr("Argument --\n"
        "Argument "+bname+"\n"
        "log\n");
  processLogOutput(cb);
}

void cvs_client::Log(const rlog_callbacks &cb,const char *file,...)
{ primeModules();
  va_list ap,ap2;
  va_start(ap,file);
  va_copy(ap2,ap);
  try {
  Log_internal(cb,file,ap);
  } catch (...)
  { W(F("trying to reconnect, perhaps the server is confused\n"));
    reconnect();
    Log_internal(cb,file,ap2);
  }
  va_end(ap);
}

void cvs_client::Log(const rlog_callbacks &cb,std::string const& file,
                                      std::vector<std::string> const& args)
{ primeModules();
  try {
  Log_internal(cb,file,args);
  } catch (...)
  { W(F("trying to reconnect, perhaps the server is confused\n"));
    reconnect();
    Log_internal(cb,file,args);
  }
}

// dummy is needed to satisfy va_start (cannot pass objects of non-POD type)
void cvs_client::RLog(const rlog_callbacks &cb,bool dummy,...)
{ primeModules();
  { va_list ap;
    va_start(ap,dummy);
    SendCommand("rlog",ap);
    va_end(ap);
  }
  processLogOutput(cb);
}

void cvs_client::processLogOutput(const rlog_callbacks &cb)
{
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
    L(FL("state %d\n") % int(state));
    I(!lresult.empty());
    MM(lresult[0].first);
    MM(lresult[0].second);
    E(lresult[0].first!="CMD" || lresult[0].second!="error", F("log failed"));
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
            begins_with(result,"Working file: ") ||
            begins_with(result,"total revisions: "))
          ;
        else if (result=="description:")
        { state=st_desc;
          description.clear();
        }
        else if (result=="symbolic names:")
          state=st_tags;
        else
        { W(F("unknown rcs head '%s'\n") % result);
        }
        break;
      }
      case st_tags:
      { std::string result=combine_result(lresult);
        I(!result.empty());
        if (result[0]!='\t') 
        { L(FL("result[0] %d %d\n") % result.size() % int(result[0])); state=st_head; goto reswitch; }
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
        if (!begins_with(result,"revision ",len)) // do not delete the space here
                    // it restricts accepted lines further
        // accept ---------------------------- lines in changelogs
        { description+=std::string(revisionend)+"\n"; 
          state=st_desc;
          goto reswitch;
        }
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
        { // actually I encountered 7,10,11,14,15
          I(lresult.size()>=7);
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

cvs_client::checkout cvs_client::CheckOut(const std::string &_file, const std::string &revision)
{ primeModules();
  std::string file=_file;
  struct checkout result;
  MM(file);
  MM(revision);
  // Directory("");
  std::string usemodule=module;
  { std::map<std::string,std::string>::reverse_iterator i;
    unsigned len=0;
    for (i=server_dir.rbegin();i!=server_dir.rend();++i)
    { if (begins_with(file,i->first,len)) break;
    }
    I(i!=server_dir.rend());
    if (!i->first.empty()) 
    { usemodule=i->first;
      if (usemodule[usemodule.size()-1]=='/') 
        usemodule.erase(usemodule.size()-1,1);
      usemodule=basename(usemodule);
      file.erase(0,i->first.size());
      L(FL("usemodule %s @%s %s /%s\n") % _file % i->first % usemodule % file);
    }
  }
  SendCommand("co",/*"-N","-P",*/"-r",revision.c_str(),"--",(usemodule+"/"+file).c_str(),(void*)0);
  enum { st_co
       } state=st_co;
  std::vector<std::pair<std::string,std::string> > lresult;
  std::string dir,dir2,rcsfile;
  while (fetch_result(lresult))
  { switch(state)
    { case st_co:
      { I(!lresult.empty());
        if (lresult[0].first=="CMD")
        { E(lresult[0].second!="error", F("failed to check out %s\n") % file);
          if (lresult[0].second=="Clear-sticky")
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
            result.mod_time=mod_time2time_t(lresult[1].second);
          }
          else if (lresult[0].second=="Created"
              || lresult[0].second=="Update-existing")
          // Update-existing results after crossing a dead state
          { // std::cerr << combine_result(lresult) << '\n';
            I(lresult.size()==7);
            I(lresult[6].first=="data");
            I(lresult[3].first=="new entries line");
            std::string new_revision;
            parse_entry(lresult[3].second,new_revision,result.keyword_substitution);
            result.mode=lresult[4].second;
            result.contents=lresult[6].second;
            L(FL("file %s revision %s: %d bytes\n") % file 
                % revision % lresult[6].second.size());
          }
          else if (lresult[0].second=="Template")
          { I(lresult.size()==5);
            I(lresult[3].first=="length");
            long len = boost::lexical_cast<long>(lresult[3].second.c_str());
            I(len >= 0);
            I(lresult[4].second.size() == (size_t) len);
            L(FL("found commit template %s:\n%s") % lresult[2].second % lresult[4].second);
            // FIX actually do something with the template?
            result.committemplate = lresult[4].second;
          }
          else
          { W(F("CheckOut: unrecognized CMD %s\n") % lresult[0].second);
          }
        }
        else if (lresult[0].second=="+updated")
        { // std::cerr << combine_result(lresult) << '\n';
        }
        else 
        { W(F("CheckOut: unrecognized response %s\n") % lresult[0].second);
        }
        break;
      }
    }
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
      if (line.size()>=root.size()+2 && begins_with(line,root)
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
  for (std::map<std::string,std::string>::const_reverse_iterator i=server_dir.rbegin();
        i!=server_dir.rend();++i)
  { if (begins_with(file,i->second))
    { file.replace(0,i->second.size(),i->first);
      // remove additional slashes (e.g. sourceforge gc-linux)
      while (file.size()>i->first.size() && file[i->first.size()]=='/')
        file.erase(i->first.size(),1);
      break;
    }
  }
  if (file.size()>2 && file.substr(file.size()-2)==",v") file.erase(file.size()-2,2);
  std::string::size_type lastslash=file.rfind('/');
  if (lastslash!=std::string::npos && lastslash>=5
          && file.substr(lastslash-5,6)=="Attic/")
    file.erase(lastslash-5,6);
  return file;
}

namespace {
struct store_here : cvs_client::update_callbacks
{ cvs_client::update &store;
  store_here(cvs_client::update &s) : store(s) {}
  virtual void operator()(const cvs_client::update &u) const
  { const_cast<cvs_client::update&>(store)=u;
  }
};
}

cvs_client::update cvs_client::Update(const std::string &file, 
            const std::string &old_revision, const std::string &new_revision,
            const std::string &keyword_expansion)
{
  struct update result;
  std::vector<update_args> file_revision;
  file_revision.push_back(update_args(file,old_revision,new_revision,keyword_expansion));
  Update(file_revision,store_here(result));
  return result;
}

cvs_client::update cvs_client::Update(const std::string &file, const std::string &new_revision)
{
  struct update result;
  std::vector<update_args> file_revision;
  file_revision.push_back(update_args(file,"",new_revision,""));
  Update(file_revision,store_here(result));
  return result;
}

// we have to update, status will give us only strange strings (and uses too
// much bandwidth?) [is too verbose]
void cvs_client::Update(const std::vector<update_args> &file_revisions,
    const update_callbacks &cb)
{ primeModules();
  struct update result;
  I(!file_revisions.empty());
  std::string olddir;
  for (std::vector<update_args>::const_iterator i=file_revisions.begin(); 
                        i!=file_revisions.end(); ++i)
  { if (dirname(i->file)!=olddir)
    { olddir=dirname(i->file);
      Directory(olddir);
    }
    if (!i->old_revision.empty())
    { std::string bname=basename(i->file);
      std::string branchpart;
      if (!branch.empty()) branchpart="T"+branch;
      writestr("Entry /"+bname+"/"+i->old_revision+"//"
                +i->keyword_substitution+"/"+branchpart+"\n");
      writestr("Unchanged "+bname+"\n");
    }
  }
  if (file_revisions.size()==1 && !file_revisions.begin()->new_revision.empty())
  { std::vector<std::string> args;
    args.push_back("-d"); // create new directories
    args.push_back("-C"); // ignore previous context
    if (file_revisions.begin()->old_revision.empty())
    {
      if (branch.empty())
        args.push_back("-A");
    }
    else
      args.push_back("-u"); // send back diff
    args.push_back("-r");
    args.push_back(file_revisions.begin()->new_revision);
    args.push_back(basename(file_revisions.begin()->file));
    SendCommand(std::string("update"),args);
  }
  else 
  { std::vector<std::string> args;
    args.push_back("-d"); // create new directories
    args.push_back("-C"); // ignore previous context
    args.push_back("-u"); // send back diff
    if (!branch.empty())
      args.push_back("-r"+branch);
    Directory("."); // needed for 1.11
    SendCommand(std::string("update"),args);
  }
  std::vector<std::pair<std::string,std::string> > lresult;
  std::string dir,dir2,rcsfile;
  enum { st_normal, st_merge } state=st_normal;

  bool confused=false;
  while (fetch_result(lresult))
  { I(!lresult.empty());
    unsigned len=0;
    if (lresult[0].first=="CMD")
    { if (lresult[0].second=="Created" || lresult[0].second=="Update-existing")
      { I(lresult.size()==7);
        I(lresult[6].first=="data");
        dir=lresult[1].second;
        result.file=lresult[2].second;
        I(!result.file.empty());
        if (result.file[0]=='/') result.file=rcs_file2path(result.file);
        else // relative path name - relative to what?
          result.file=rcs_file2path(root+"/"+result.file);
        result.contents=lresult[6].second;
        parse_entry(lresult[3].second,result.new_revision,result.keyword_substitution);
        cb(result);
        result=update();
        state=st_normal;
      }
      else if (lresult[0].second=="Rcs-diff")
      { I(lresult.size()==7);
        I(lresult[6].first=="data");
        dir=lresult[1].second;
        result.file=lresult[2].second;
        I(!result.file.empty());
        if (result.file[0]=='/') result.file=rcs_file2path(result.file);
        else // relative path name - relative to what?
          result.file=rcs_file2path(root+"/"+result.file);
        result.patch=lresult[6].second;
        parse_entry(lresult[3].second,result.new_revision,result.keyword_substitution);
        cb(result);
        result=update();
        state=st_normal;
      }
      else if (lresult[0].second=="Checksum")
      { I(lresult.size()==2);
        I(lresult[1].first=="data");
        result.checksum=lresult[1].second;
      }
      else if (lresult[0].second=="Removed")
      { I(lresult.size()==3);
        result.file=lresult[2].second;
        I(!result.file.empty());
        if (result.file[0]=='/') result.file=rcs_file2path(result.file);
        else // relative path name - relative to what?
          result.file=rcs_file2path(root+"/"+result.file);
        result.removed=true;
        cb(result);
        result=update();
        state=st_normal;
      }
      else if (lresult[0].second=="Clear-static-directory"
          || lresult[0].second=="Clear-template"
          || lresult[0].second=="Clear-sticky")
      { 
      }
      else if (lresult[0].second=="Copy-file")
      { I(state==st_merge);
      }
      else if (lresult[0].second=="Mod-time")
      { result.mod_time=mod_time2time_t(lresult[1].second);
      }
      else if (lresult[0].second=="Merged")
      { I(state==st_merge);
        I(lresult.size()==7);
        I(lresult[6].first=="data");
        dir=lresult[1].second;
        result.file=lresult[2].second;
        I(!result.file.empty());
        if (result.file[0]=='/') result.file=rcs_file2path(result.file);
        else // relative path name - relative to what?
          result.file=rcs_file2path(root+"/"+result.file);
        result.contents=lresult[6].second; // strictly this is unnecessary ...
        parse_entry(lresult[3].second,result.new_revision,result.keyword_substitution);
        E(false, F("Update ->%s of %s exposed CVS bug\n") 
            % result.new_revision % result.file);
      }
      else if (lresult[0].second=="error")
      { I(state==st_merge);
        break;
      }
      else if (lresult[0].second=="Checked-in")
      { confused=true;
      }
      else
      { W(F("Update: unrecognized CMD %s\n") % lresult[0].second);
      }
    }
    else if (lresult[0].second=="+updated")
    { // std::cerr << combine_result(lresult) << '\n';
      state=st_normal;
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
    else if (lresult[0].second=="? ")
    { I(lresult.size()==2);
      I(lresult[1].first=="fname");
      W(F("cvs erraneously reports ? %s\n") % lresult[1].second);
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
    { W(F("Update: unrecognized response %s\n") % lresult[0].second);
    }
  }
  if (confused) // cvs is an awfully stated machine ...
  { reconnect();
    Update(file_revisions,cb);
  }
}

void cvs_client::parse_entry(const std::string &line, std::string &new_revision, 
                            std::string &keyword_substitution)
{
  MM(line);
  std::vector<std::string> parts;
  stringtok(parts,line,"/");
  // empty last part will not get created
  if (parts.size()==5) parts.push_back(std::string());
  I(parts.size()==6);
  new_revision=parts[2];
  keyword_substitution=parts[4];
}

std::map<std::string,std::pair<std::string,std::string> >
         cvs_client::Commit(const std::string &changelog, time_t when, 
                    const std::vector<commit_arg> &commits)
{ primeModules();
  std::string olddir;
  I(!commits.empty());
// undocumented ...
//  if (CommandValid("Command-prep")) writestr("Command-prep commit\n");
  for (std::vector<commit_arg>::const_iterator i=commits.begin(); 
                        i!=commits.end(); ++i)
  { if (dirname(i->file)!=olddir)
    { olddir=dirname(i->file);
      Directory(olddir);
    }
    std::string bname=basename(i->file);
    std::string branchpart;
    if (!branch.empty()) branchpart="T"+branch;
    writestr("Entry /"+bname+"/"+(i->removed?"-":"")
          +i->old_revision+"//"+i->keyword_substitution+"/"
          +branchpart+"\n");
    if (!i->removed)
    { writestr("Checkin-time "+time_t2rfc822(when)+"\n");
      writestr("Modified "+bname+"\n");
      writestr("u=rw,g=r,o=r\n"); // standard mode
      // do _not_ translate this into locale format (e.g. F() )
      writestr(boost::lexical_cast<std::string>(i->new_content.size())+"\n");
      writestr(i->new_content);
    }
  }
  Directory(".");
  writestr("Argument -m\n");
  SendArgument(changelog);
  writestr("Argument --\n");
  for (std::vector<commit_arg>::const_iterator i=commits.begin(); 
                        i!=commits.end(); ++i)
    writestr("Argument "+i->file+"\n");
  writestr("ci\n");
  std::map<std::string,std::pair<std::string,std::string> > result;
  // process result
  std::vector<std::pair<std::string,std::string> > lresult;

  while (fetch_result(lresult))
  { I(!lresult.empty());
    unsigned len=0;
    if (lresult[0].first=="CMD")
    { if (lresult[0].second=="Mode")
        ; // who cares
      else if (lresult[0].second=="Checked-in")
      { I(lresult.size()==4);
        I(lresult[2].first=="rcs");
        I(lresult[3].first=="new entries line");
        std::pair<std::string,std::string> p;
        std::string file=lresult[2].second;
        I(!file.empty());
        if (file[0]=='/') file=rcs_file2path(file);
        else // relative path name - relative to what?
          file=rcs_file2path(root+"/"+file);
        parse_entry(lresult[3].second,p.first,p.second);
        result[file]=p;
      }
      else if (lresult[0].second=="Remove-entry")
      { I(lresult.size()==3);
        I(lresult[2].first=="rcs");
        std::string file=lresult[2].second;
        I(!file.empty());
        if (file[0]=='/') file=rcs_file2path(file);
        else // relative path name - relative to what?
          file=rcs_file2path(root+"/"+file);
        result[file]=std::make_pair(std::string(),std::string());
      }
      else if (lresult[0].second=="Mod-time")
      { I(lresult.size()==2);
        I(lresult[1].first=="date");
        W(F("Commit: Mod-time %s\n") % lresult[1].second);
      }
      else if (lresult[0].second=="Update-existing")
      // perhaps a reaction to $something$ and -kk
      { I(lresult.size()==7);
        I(lresult[6].first=="data");
        I(lresult[2].first=="rcs");
        I(lresult[3].first=="new entries line");
//        result.mode=lresult[4].second;
        std::pair<std::string,std::string> p;
        std::string file=lresult[2].second;
        I(!file.empty());
        if (file[0]=='/') file=rcs_file2path(file);
        else // relative path name - relative to what?
          file=rcs_file2path(root+"/"+file);
        parse_entry(lresult[3].second,p.first,p.second);
        result[file]=p;
        W(F("Commit: Update-existing %s rev.%s%s (%db)\n") % file 
            % p.first % p.second % lresult[6].second.size());
      }
      else if (lresult[0].second=="error")
        return std::map<std::string,std::pair<std::string,std::string> >();
      else
      { W(F("Commit: unrecognized CMD %s\n") % lresult[0].second);
      }
    }
    else if (lresult[0].second.empty())
    { I(!lresult[0].second.empty());
    }
    else if (lresult[0].second[0]=='/')
    // /cvsroot/test/F,v  <--  F
    { L(FL("%s\n") % lresult[0].second);
    }
    else if (begins_with(lresult[0].second,"new revision:",len)
        || begins_with(lresult[0].second,"initial revision:",len)
        || begins_with(lresult[0].second,"RCS file:",len)
        || begins_with(lresult[0].second,"done",len)
        || begins_with(lresult[0].second,"Removing ",len)
        || begins_with(lresult[0].second,"Checking in ",len))
    { L(FL("%s\n") % lresult[0].second);
    }
    else 
    { W(F("Commit: unrecognized response %s\n") % lresult[0].second);
    }
  }
  return result;
}

void cvs_client::SendArgument(const std::string &a)
{ // send each line separately (Argument,[Argumentx[,...]])
  std::string::size_type newline=0,start=0;
  std::string::size_type size_of_a=a.size();
  while ((newline=a.find('\n',start))!=std::string::npos)
  { writestr("Argument"+std::string(start?"x":"")+" "+a.substr(start,newline-start)+"\n");
    start=newline+1;
    if (start==size_of_a) break;
  }
  writestr("Argument"+std::string(start?"x":"")+" "+a.substr(start)+"\n");
}

std::vector<std::string> cvs_client::ExpandModules()
{ SendCommand("expand-modules",module.c_str(),(void*)0);
  std::vector<std::string> result;
  std::vector<std::pair<std::string,std::string> > lresult;
  while (fetch_result(lresult))
  { if (lresult.size()==1 && lresult[0].first=="CMD" && lresult[0].second=="error")
      E(false, F("error accessing CVS module %s\n") % module);
    I(lresult.size()==2);
    I(lresult[0].second=="Module-expansion");
    result.push_back(lresult[1].second);
  }
  return result;
}

// if you know a more efficient way to get this, feel free to replace it
std::map<std::string,std::string> cvs_client::RequestServerDir()
{ if (server_dir.size()<=1) 
    SendCommand("co","-l","-r9999",module.c_str(),(void*)0);
  else SendCommand("co","-r9999",module.c_str(),(void*)0);
  std::string last_local,last_rcs;
  std::map<std::string,std::string> result;
  std::vector<std::pair<std::string,std::string> > lresult;
  while (fetch_result(lresult))
  { I(!lresult.empty());
    I(lresult[0].first=="CMD");
    if (lresult[0].second=="Set-sticky"
        || lresult[0].second=="Clear-template"
        || lresult[0].second=="Set-static-directory"
        || lresult[0].second=="Template") continue;
    if (lresult[0].second!="Clear-static-directory")
      L(FL("cvs_client::RequestServerDir lresult[0].second is '%s', not 'Clear-static-directory'") % lresult[0].second);
    I(lresult[0].second=="Clear-static-directory");
    I(lresult.size()==3);
#if 1 // I'm not quite sure this is correct    
    if (!lresult[2].second.empty() && lresult[2].second[0]!='/')
      // relative path, prepend repository (?)
    { lresult[2].second=root+"/"+lresult[2].second;
    }
#endif
    if (!last_rcs.empty() && begins_with(lresult[2].second,last_rcs)
          && lresult[1].second.substr(0,last_local.size())==last_local)
    { I(lresult[2].second.substr(last_rcs.size())
            ==lresult[1].second.substr(last_local.size()));
      continue;
    }
    result[shorten_path(lresult[1].second)]=lresult[2].second;
    last_local=lresult[1].second;
    last_rcs=lresult[2].second;
  }
  return result;  
}

void cvs_client::SetServerDir(const std::map<std::string,std::string> &m)
{ server_dir=m;
}

void cvs_client::primeModules()
{ if (!server_dir.empty()) return;
  std::vector<std::string> modules=ExpandModules();
  for (std::vector<std::string>::const_iterator i=modules.begin();
          i!=modules.end();++i)
  { server_dir[shorten_path(*i)];
  }
  server_dir=RequestServerDir();
  for (std::map<std::string,std::string>::const_iterator i=server_dir.begin();
      i!=server_dir.end();++i)
    L(FL("server dir %s -> %s") % i->first % i->second);
  // it is nearly certain that the server will be in a strange state now
  // so reconnect
  reconnect();
}

void cvs_client::AddDirectory(std::string const& name, std::string const& _parent)
{ std::string parent=_parent;
  if (parent.empty()) parent=".";
  if (parent!=".") primeModules();
  else server_dir[""]=module;
  Directory(parent!="." ? (parent+"/"+name) : name);
  Directory(parent);
  SendCommand(std::string("add"),std::vector<std::string>(1,name));
}
