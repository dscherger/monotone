// Copyright 2005   Timothy Brownawell   <tbrownaw@gmail.com>
// Licensed to the public under the GNU GPL v2, see COPYING

#include "monotone.hh"
#include <gtkmm.h>

#ifdef WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <csignal>
#endif

#include <map>

#include <boost/lexical_cast.hpp>
#include <sstream>
#include <iostream>

inline int max(int a, int b) {return (a>b)?a:b;}

monotone::monotone(): pid(0), dir("."), done(2)
{
}

monotone::~monotone()
{
  stop();
}

namespace {
bool process_packet(string & from, string & out)
{ // cmdnum, err, more, size
  int c1 = from.find(':');
  int c2 = from.find(':', c1+1);
  int c3 = from.find(':', c2+1);
  int c4 = from.find(':', c3+1);
  if (c1 == string::npos || c2 == string::npos ||
      c3 == string::npos || c4 == string::npos)
    return false;
  if (!(c1 < c2 && c2 < c3 && c3 < c4))
    return false;
  string sub = from.substr(c3+1, c4-c3-1);
  int size = boost::lexical_cast<int>(sub);
  if (size + c4 + 1> from.size())
    return false;
  out += from.substr(c4 + 1, size);
  bool done = from[c2+1] == 'l';
  from = from.substr(size + c4 + 1);
  return done;
}
bool process_packets(string & from, string & out)
{
  int s;
  bool r;
  do
    {
      s = from.size();
      r = process_packet(from, out);
    }
  while (!r && s != from.size());
  return r;
}
}

bool monotone::got_data(Glib::IOCondition c, Glib::RefPtr<Glib::IOChannel> chan)
{
  if (c == Glib::IO_HUP)
    {
      if (++done == 2)
        child_exited(0, 0);
      return false;
    }
  Glib::ustring data;
  chan->read(data, 1024);
  if (mode == STDIO)
    {
      tempstr += data;
      bool last = process_packets(tempstr, output_std);
      if (last)
        {
          child_exited(0, 0);
        }
      return true;
    }
  else
    {
      output_std += data;
      return true;
    }
}

bool monotone::got_err(Glib::IOCondition c, Glib::RefPtr<Glib::IOChannel> chan)
{
  if (c == Glib::IO_HUP)
    {
      if (++done == 2)
        child_exited(0, 0);
      return false;
    }
  Glib::ustring data;
  chan->read(data, 1024);
  output_err += data;
  return true;
}

void monotone::setup_callbacks()
{
  done = 0;
  {
    Glib::RefPtr<Glib::IOChannel> ioc = Glib::IOChannel::create_from_fd(from);
    ioc->set_flags(Glib::IO_FLAG_NONBLOCK);
    ioc->set_encoding("");
    ioc->set_buffered(false);
    Glib::RefPtr<Glib::IOSource> ios = Glib::IOSource::create(ioc, Glib::IO_IN | Glib::IO_HUP);
    ios->connect(sigc::bind(sigc::mem_fun(*this, &monotone::got_data), ioc));
    ios->attach(Glib::MainContext::get_default());
  }
  {
    Glib::RefPtr<Glib::IOChannel> ioc = Glib::IOChannel::create_from_fd(errfrom);
    ioc->set_flags(Glib::IO_FLAG_NONBLOCK);
    ioc->set_encoding("");
    ioc->set_buffered(false);
    Glib::RefPtr<Glib::IOSource> ios = Glib::IOSource::create(ioc, Glib::IO_IN | Glib::IO_HUP);
    ios->connect(sigc::bind(sigc::mem_fun(*this, &monotone::got_err), ioc));
    ios->attach(Glib::MainContext::get_default());
  }
}

bool
monotone::execute(vector<string> args)
{
  if (done < 2)
    {
      //std::cerr<<"Not spawning, pipes still open!\n";
      return false;
    }
  //std::cerr<<"spawn()\n";
  args.insert(args.begin(), Glib::find_program_in_path("monotone"));
  if (!db.empty())
    args.push_back("--db=" + db);
  try
    {
      Glib::spawn_async_with_pipes(dir, args, Glib::SPAWN_DO_NOT_REAP_CHILD,
                                   sigc::slot<void>(), &pid,
                                   &to, &from, &errfrom);
//      Glib::signal_child_watch().connect(sigc::mem_fun(*this, &monotone::child_exited), pid);
    }
  catch (Glib::SpawnError & e)
    {
      std::cerr<<"Spawn error.\n";
      return false;
    }
  setup_callbacks();
  return true;
}

void
monotone::child_exited(Glib::Pid p, int c)
{
  stopped();
  signal_done.emit();
  busy = false;
  signal_done.clear();
  output_std.clear();
  output_err.clear();
}

bool
monotone::start()
{
  stop();
  while (done < 2) Gtk::Main::iteration();
  mode = STDIO;
  std::vector<std::string> args;
  args.push_back("automate");
  args.push_back("stdio");
  return execute(args);
}
bool
monotone::stop()
{
  if (!pid)
    {
      //std::cerr<<"Already stopped.\n";
      //if (busy) std::cerr<<"\tBut still busy!\n";
      return false;
    }
  //std::cerr<<"kill()\n";
#ifdef WIN32
  TerminateProcess(pid, 0);
#else
  kill(pid, SIGKILL);
  int r;
  do {r = waitpid(pid, 0, 0);} while (r==-1 && errno == EINTR);
#endif
  Glib::spawn_close_pid(pid);
  pid = 0;
  return true;
}

bool
monotone::stopped()
{
  if (!pid)
    return true;
#ifdef WIN32
  DWORD r = WaitForSingleObject(pid, 0);
  if (r == WAIT_TIMEOUT)
    return false;
#else
  int r = waitpid(pid, 0, WNOHANG);
  if (r == 0)
    return false;
#endif
  Glib::spawn_close_pid(pid);
  pid = 0;
  return true;
}

bool
monotone::is_busy()
{
//  if (busy && stopped())
//    child_exited(0, 0);
  return busy;
}

void
monotone::waitfor()
{
  while(busy && (!stopped() || Gtk::Main::events_pending()))
    {
      Gtk::Main::iteration();
    }
  if (busy)
    child_exited(0, 0);
}

void
monotone::command(string const & cmd,
                  vector<string> const & args)
{
  if (!pid || mode != STDIO)
    start();
  mode = STDIO;
  busy = true;
  std::ostringstream s;
  s << cmd.size() <<":"<<cmd;
  for (std::vector<std::string>::const_iterator i = args.begin();
        i != args.end(); ++i)
    s << i->size() << ":" << *i;
  std::string c = "l" + s.str() + "e";
  Glib::RefPtr<Glib::IOChannel> chan = Glib::IOChannel::create_from_fd(to);
  chan->write(c);
  chan->flush();
}

void
monotone::runcmd(string const & cmd,
                 vector<string> const & args)
{
  stop();// stop stdio
  while (done < 2) Gtk::Main::iteration();
  mode = EXEC;
  std::vector<std::string> argg = args;
  argg.insert(argg.begin(), cmd);
  execute(argg);
  busy = true;
}

namespace {
  void process_inventory(string * resp, vector<inventory_item> * outp)
  {
    string & res(*resp);
    vector<inventory_item> & out(*outp);
    try
      {
        std::map<int, int> renames;
        int begin = 0;
        int end = res.find_first_of("\r\n", begin);
        while (begin < res.size() && begin >= 0)
          {
            int sp1 = begin + 4;
            int sp2 = res.find(' ', sp1 + 1);
            int sp3 = res.find(' ', sp2 + 1);
            if (sp1 >= res.size() || sp2 == string::npos || sp3 == string::npos)
              {
                begin = -1;
                continue;
              }
            int fromid = boost::lexical_cast<int>(res.substr(sp1, sp2-sp1));
            int toid = boost::lexical_cast<int>(res.substr(sp2+1, sp3-sp2-1));
            std::string path = res.substr(sp3+1, end - sp3-1);
            inventory_item *pre, *post;
            if (!fromid || !toid)
              out.push_back(inventory_item());
            if (!fromid)
              pre = &out.back();
            if (!toid)
              post = &out.back();
            if (fromid)
              {
                std::map<int, int>::iterator i = renames.find(fromid);
                if (i != renames.end())
                  pre = &out[i->second];
                else
                  {
                    renames.insert(std::make_pair(fromid, out.size()));
                    out.push_back(inventory_item());
                    pre = &out.back();
                  }
              }
            if (toid)
              {
                std::map<int, int>::iterator i = renames.find(toid);
                if (i != renames.end())
                  post = &out[i->second];
                else
                  {
                    renames.insert(std::make_pair(toid, out.size()));
                    out.push_back(inventory_item());
                    post = &out.back();
                  }
              }
            switch (res[begin])//prestate
              {
              case 'D':
              case 'R':
                pre->prename = path;
              default:
                ;
              }
            switch (res[begin+1])//poststate
              {
              case 'R':
              case 'A':
                post->postname = path;
                break;
              default:
                if (pre->prename.empty())
                  {
                    post->postname = path;
                    pre->prename = path;
                  }
              }
            switch (res[begin+2])//filestate
              {
              case 'M':
                post->state = inventory_item::missing;
                break;
              case 'P':
                post->state = inventory_item::patched;
                break;
              case 'I':
                if (!fromid && !toid)
                  post->state = inventory_item::ignored;
                break;
              case 'U':
                if (!fromid && !toid)
                  post->state = inventory_item::unknown;
                break;
              default:
                ;
              }
            begin = end + 1;
            if (end == string::npos)
              begin = -1;
            end = res.find_first_of("\r\n", begin);
          }
      } catch (std::exception &) {
        std::cerr<<"Exception while reading inventory.\n"<<res;
      }
  }
};

void
monotone::inventory(std::vector<inventory_item> & out)
{
  out.clear();
  waitfor();
  std::vector<std::string> args;
  command("inventory", args);
  signal_done.connect(sigc::bind(sigc::ptr_fun(&process_inventory), &output_std, &out));
}

namespace {
  void process_certs(string * resp, vector<cert> * outp)
  {
    string & res(*resp);
    vector<cert> & out(*outp);
    cert c;
    for (int begin = 0, end = res.find('\n'); begin != res.size();
          begin = end + 1, end = res.find('\n', begin))
      {
        std::string line = res.substr(begin, end-begin);
        int lpos = line.find('"');// other is at end of line
        if (lpos == std::string::npos)
          {
            out.push_back(c);
            continue;
          }
        while (line[line.size()-1] != '"' || line[line.size()-2] =='\\')
          {
            begin = end + 1;
            end = res.find('\n', begin);
            line += "\n" + res.substr(begin, end-begin);
          }
        std::string contents;
        for (int i = lpos+1; i < line.size() && line[i] != '"'; ++i)
          {
            if (line[i] == '\\')
              ++i;
            contents += line[i];
          }
        if (line.find("key") < lpos)
          c.key = contents;
        else if (line.find("signature") < lpos)
          c.sig = (contents == "ok");
        else if (line.find("name") < lpos)
          c.name = contents;
        else if (line.find("value") < lpos)
          c.value = contents;
        else if (line.find("trust") < lpos)
          c.trusted = (contents == "trusted");
      }
    if (res.size())
      out.push_back(c);
  }
};

void
monotone::certs(std::string const & rev, vector<cert> & out)
{
  std::vector<std::string> args;
  args.push_back(rev);
  command("certs", args);
  signal_done.connect(sigc::bind(sigc::ptr_fun(&process_certs), &output_std, &out));
}

namespace {
  void process_select(string * resp, vector<string> * outp)
  {
    string & res(*resp);
    vector<string> & out(*outp);
    int begin = 0;
    int end = res.find('\n', begin);
    while (begin != res.size())
      {
        out.push_back(res.substr(begin, end-begin));
        begin = end + 1;
        end = res.find('\n', begin);
      }
  }
};

void
monotone::select(string const & sel, vector<string> & out)
{
  vector<string> args;
  args.push_back(sel);
  command("select", args);
  signal_done.connect(sigc::bind(sigc::ptr_fun(&process_select), &output_std, &out));
}

void
monotone::make_cert(string const & rev,
                    string const & name,
                    string const & value)
{
  vector<string> args;
  args.push_back(rev);
  args.push_back(name);
  args.push_back(value);
  runcmd("cert", args);
}

namespace {
  void process_commit(string * resp, string * outp)
  {
    string & res(*resp);
    string & out(*outp);
    int s = 0;
    for (int c = 0; c < res.size(); ++c)
      {
        if (string("abcdefABCDEF0123456789").find(res[c]) == string::npos)
          s = c+1;
        if (c - s + 1 == 40)
          {
            out = res.substr(s, 40);
            break;
          }
      }
  }
};

void
monotone::commit(vector<string> args, string & rev)
{
  runcmd("commit", args);
  signal_done.connect(sigc::bind(sigc::ptr_fun(&process_commit), &output_err, &rev));
}

namespace {
  void process_noop(string * from, string * to)
  {
    *to = *from;
  }
};

void
monotone::diff(std::string const & filename, string & out)
{
  std::vector<std::string> args;
  args.push_back(filename);
  runcmd("diff", args);
  signal_done.connect(sigc::bind(sigc::ptr_fun(&process_noop), &output_std, &out));
}

void
monotone::diff(string const & filename,
               string const & rev1,
               string const & rev2,
               string & out)
{
  std::vector<std::string> args;
  args.push_back(filename);
  args.push_back("--revision=" + rev1);
  args.push_back("--revision=" + rev2);
  runcmd("diff", args);
  signal_done.connect(sigc::bind(sigc::ptr_fun(&process_noop), &output_std, &out));
}

void
monotone::cat(string const & filename, string const & rev, string & out)
{
  vector<string> args;
  args.push_back(filename);
  args.push_back("--revision=" + rev);
  runcmd("cat", args);
  signal_done.connect(sigc::bind(sigc::ptr_fun(&process_noop), &output_std, &out));
}

void
monotone::get_revision(string const & rev, string & out)
{
  std::vector<std::string> args;
  args.push_back(rev);
  command("get_revision", args);
  signal_done.connect(sigc::bind(sigc::ptr_fun(&process_noop), &output_std, &out));
}

void
monotone::get_manifest_of(string const & rev, string & out)
{
  std::vector<std::string> args;
  args.push_back(rev);
  command("get_manifest_of", args);
  signal_done.connect(sigc::bind(sigc::ptr_fun(&process_noop), &output_std, &out));
}

void
monotone::add(string const & file)
{
  vector<string> args;
  args.push_back(file);
  runcmd("add", args);
}

void
monotone::drop(string const & file)
{
  vector<string> args;
  args.push_back(file);
  runcmd("drop", args);
}

void
monotone::revert(string const & file)
{
  vector<string> args;
  args.push_back(file);
  runcmd("revert", args);
}

void
monotone::rename(string const & oldname, string const & newname)
{
  vector<string> args;
  args.push_back("--execute");
  args.push_back(oldname);
  args.push_back(newname);
  runcmd("rename", args);
}

namespace {
  void process_update(vector<string> * opts, string * in, string * out)
  {
    opts->clear();
    *out = *in;
    int p = out->find("multiple update candidates");
    if (p == std::string::npos)
      return;
  
    p = out->find("monotone:   ", p + 1);
    while (p != std::string::npos)
      {
        p = out->find(":", p);
        p = out->find_first_not_of(" ", p + 1);
        opts->push_back(out->substr(p, 40));
        p = out->find("monotone:   ", p + 1);
      }
  }
};

void
monotone::update(vector<string> & opts, string & out)
{
  opts.clear();
  std::string ign;
  std::vector<std::string> args;
  runcmd("update", args);
  signal_done.connect(sigc::bind(sigc::ptr_fun(&process_update), &opts, &output_err, &out));
}

void
monotone::update(std::string const & rev, string & out)
{
  std::string ign;
  std::vector<std::string> args;
  args.push_back("--revision="+rev);
  runcmd("update", args);
  signal_done.connect(sigc::bind(sigc::ptr_fun(&process_noop), &output_err, &out));
}

void
monotone::sync()
{
  string ign;
  vector<string> args;
  args.push_back("--ticker=count");
  runcmd("sync", args);
}
