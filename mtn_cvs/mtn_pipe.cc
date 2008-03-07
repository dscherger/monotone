// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2006 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "mtn_pipe.hh"
#include <iterator>
#include <boost/lexical_cast.hpp>
#include "stringtok.hh"
#include <stdexcept>

#ifndef TEST_MAIN
#include <sanity.hh>
#endif

void mtn_pipe::open(std::string const& command, std::vector<std::string> const& options)
{ std::vector<std::string> args;
//  args.push_back("--db="+database);
  std::copy(options.begin(),options.end(),std::back_inserter(args));
  args.push_back("automate");
  args.push_back("stdio");
  pipe=new Netxx::PipeStream(command,args);
}

void mtn_pipe::close()
{ if (pipe)
  { delete pipe;
    pipe=0;
  }
}

// I hate C interfaces !
Netxx::PipeStream &operator<<(Netxx::PipeStream &s, char c)
{ s.write(&c,1);
  return s;
}

Netxx::PipeStream &operator<<(Netxx::PipeStream &s, std::string const &t)
{ s.write(t.data(),t.size());
  return s;
}

Netxx::PipeStream &operator<<(Netxx::PipeStream &s, char const x[])
{ s.write(x,strlen(x));
  return s;
}

template <class T>
 Netxx::PipeStream &operator<<(Netxx::PipeStream &s, T const& i)
{ return s << boost::lexical_cast<std::string>(i);
}

#ifdef TEST_MAIN
#include <cassert>
#undef I
#define I(x) assert(x)
#endif

static unsigned count_colons(char const* s, unsigned len)
{ unsigned res=0;
  for (unsigned i=0;i<len;++i) if (s[i]==':') ++res;
  return res;
}

static int blocking_read(Netxx::PipeStream &s, Netxx::PipeCompatibleProbe &p,
        char *buf, unsigned len)
{ int read=0;
  static Netxx::Timeout timeout(60L);
  while (read<len)
  { Netxx::Probe::result_type res = p.ready(timeout);
    if (!(res.second & Netxx::Probe::ready_read)) return read;
    // it looks like ready_read && !readres indicates a broken pipe ? (not -1)
    int readres=s.read(buf+read,len-read);
    if (readres<=0) return read?read:-1;
    read+=readres;
  }
  return read;
}

std::ostream &operator<<(std::ostream &os, std::vector<std::string> const& v)
{ for (std::vector<std::string>::const_iterator i=v.begin();i!=v.end();++i)
    os << *i << ',';
  return os;
}

std::string mtn_pipe::automate(std::string const& command, 
        std::vector<std::string> const& args) throw (std::runtime_error)
{ L(FL("mtn automate: %s %s") % command % args);
  std::string s_cmdnum=boost::lexical_cast<std::string>(cmdnum);
  (*pipe) << 'l' << command.size() << ':' << command;
  for (std::vector<std::string>::const_iterator i=args.begin();i!=args.end();++i)
    (*pipe) << i->size() << ':' << *i;
  (*pipe) << "e\n";
  std::string result;
  Netxx::PipeCompatibleProbe probe;
  probe.add(*pipe, Netxx::Probe::ready_read);
again:
  char buf[1024];
  // at least we can expect 8 bytes
  
  int read=blocking_read(*pipe,probe,buf,7+s_cmdnum.size());
  N(read==7+s_cmdnum.size(), F("mtn pipe failure\n"));
  int colons;
  while ((colons=count_colons(buf,read))<4 && read+(4-colons)<=sizeof(buf))
  { int res=blocking_read(*pipe,probe,buf+read,4-colons);
    I(res==4-colons);
    read+=res;
  }
  I(colons==4);
  std::vector<std::string> results;
  stringtok(results,std::string(buf,buf+read),":");
  I(results.size()==4);
  I(results[0]==s_cmdnum);
  int cmdresult=boost::lexical_cast<int>(results[1]);
  I(results[2].size()==1);
  unsigned chars=boost::lexical_cast<unsigned>(results[3]);
  while (chars)
  { unsigned toread=chars<=sizeof(buf)?chars:sizeof(buf);
    int res=blocking_read(*pipe,probe,buf,toread);
    I(res==toread);
    result+=std::string(buf,buf+toread);
    chars-=toread;
  }
  if (results[2]=="m") goto again;
  I(results[2]=="l");
  ++cmdnum;
  if (cmdresult) 
  { L(FL("mtn returned %d %s") % cmdresult % result);
    throw std::runtime_error(result);
  }
  L(FL("automate result %s") % result);
  return result;
}

#ifdef TEST_MAIN
#include <iostream>

int main(int argc, char **argv)
{ mtn_pipe p;
  if (argc==1 && std::string(argv[0])=="--help") 
  { std::cerr << "USAGE: " << argv[0] << " [binary [options]]\n";
    return 0;
  }
  std::vector<std::string> args;
  std::string cmd="mtn";
  if (argc>1) cmd=argv[1];
  for (unsigned i=2;i<argc;++i) args.push_back(argv[i]);
  p.open(cmd,args);
  std::cout << p.automate("interface_version"); // << '\n';
}
#endif
