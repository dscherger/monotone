// Copyright 2005   Timothy Brownawell   <tbrownaw@gmail.com>
// Licensed to the public under the GNU GPL v2, see COPYING

#include "monotone.hh"

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <csignal>

#include <map>

#include <boost/lexical_cast.hpp>

// For future reference: CreateProcess and WaitForMultipleObjects on win32
// to replace fork/exec and select?
// Put in a 'class Platform {};'?

inline int max(int a, int b) {return (a>b)?a:b;}

monotone::monotone(): pid(-1)
{
}

monotone::~monotone()
{
  stop();
}

bool
monotone::execute(std::vector<std::string> args)
{
  int to_mtn[2];
  int from_mtn[2];
  int err_mtn[2];
  if (pipe(to_mtn) < 0 || pipe(from_mtn) < 0 || pipe(err_mtn) < 0)
    return false;
  pid = fork();
  if (pid < 0)
    {
      close(to_mtn[0]);
      close(to_mtn[1]);
      close(from_mtn[0]);
      close(from_mtn[1]);
      close(err_mtn[0]);
      close(err_mtn[1]);
      return false;
    }
  else if (pid > 0)
    {
      close(to_mtn[0]);
      close(from_mtn[1]);
      close(err_mtn[1]);
      to = to_mtn[1];
      from = from_mtn[0];
      errfrom = err_mtn[0];
      return true;
    }
  else
    {
      if (!db.empty())
        args.push_back("--db=" + db);
      chdir(dir.c_str());
      if (close(to_mtn[1]) < 0 || close(from_mtn[0]) < 0)
        return false;
      if (close(0) < 0 || close(1) < 0 || close(2) < 0)
        return false;
      if (dup2(to_mtn[0], 0) < 0
          || dup2(from_mtn[1], 1) < 0
          || dup2(err_mtn[1], 2) < 0)
        return false;
      char **arg = new char *[args.size() + 2];
      arg[0] = "monotone";
      for (unsigned int i = 0; i < args.size(); ++i)
        {
          arg[i+1] = new char[args[i].size()+1];
          memcpy(arg[i+1], args[i].c_str(), args[i].size()+1);
        }
      arg[args.size()+1] = 0;
      int ret = execvp(arg[0], arg);
      exit(1);
    }
}
bool
monotone::start()
{
  stop();
  std::vector<std::string> args;
  args.push_back("automate");
  args.push_back("stdio");
  return execute(args);
}
bool
monotone::stop()
{
  if (pid == -1)
    return false;
  kill(pid, SIGKILL);
  int r;
  do {r = waitpid(pid, 0, 0);} while (r==-1 && errno == EINTR);
  pid = -1;
  return true;
}
bool
monotone::stopped()
{
  if (pid == -1)
    return true;
  int r = waitpid(pid, 0, WNOHANG);
  if (r == 0)
    return false;
  pid = -1;
  return true;
}

bool
monotone::read_header(int & cmdnum, int & err, bool & more, int & size)
{
  char head[32];
  int got = 0;
  int minsize = 8;
  int c1(0), c2(0), c3(0), c4(0);
  while (!c4 && !stopped())
    {
      int r = read(from, head+got, minsize-got);
      got += r;
      if (!c1)
        for (int i = 0; i < got; ++i)
          if (head[i] == ':')
            c1 = i, i=got;
      if (!c2)
        for (int i = c1+1; i < got; ++i)
          if (head[i] == ':')
            c2 = i, i=got;
      if (!c3)
        for (int i = c2+1; i < got; ++i)
          if (head[i] == ':')
            c3 = i, i=got;
      if (!c4)
        for (int i = c3+1; i < got; ++i)
          if (head[i] == ':')
            c4 = i, i=got;
      if (c4)
        minsize = c4;
      else if (c3)
        minsize = max(c3+2, got+1);
      else if (c2)
        minsize = max(c2+4, got+3);
      else if (c1)
        minsize = max(c1+6, got+5);
      else
        minsize = max(8, got+7);
    }
  if (stopped())
    return false;
  cmdnum = boost::lexical_cast<int>(std::string(head, c1));
  err = boost::lexical_cast<int>(std::string(head+c1+1, c2-c1-1));
  more = (head[c2+1] == 'm');
  size = boost::lexical_cast<int>(std::string(head+c3+1, c4-c3-1));
  return true;
}

bool
monotone::read_packet(std::string & out)
{
  int cn, er, size;
  bool m = false;
  if (!read_header(cn, er, m, size))
    return false;
  char output[size];
  int got = 0;
  do
    {
      int r = read(from, output, size - got);
      got += r;
      out += std::string(output, r);
    } while (got != size && !stopped());
  if (stopped())
    return false;
  return m;
}
std::string
monotone::command(std::string const & cmd,
                    std::vector<std::string> const & args)
{
  if (pid == -1)
    start();
  std::string res;
  std::ostringstream s;
  s << cmd.size() <<":"<<cmd;
  for (std::vector<std::string>::const_iterator i = args.begin();
        i != args.end(); ++i)
    s << i->size() << ":" << *i;
  std::string c = "l" + s.str() + "e";
  write(to, c.c_str(), c.size());
  while(read_packet(res) && !stopped())
    ;
  return res;
}

void
monotone::runcmd(std::string const & cmd,
                 std::vector<std::string> const & args,
                 std::string & out, std::string & err)
{
  out.clear();
  err.clear();
  stop();// stop stdio
  std::vector<std::string> argg = args;
  argg.insert(argg.begin(), cmd);
  execute(argg);

  int size = 2048;
  char *output = new char[size];
  char *errout = new char[size];
  fd_set rd, ex;
  do
    {
      FD_ZERO(&rd);
      FD_ZERO(&ex);
      FD_SET(from, &rd);
      FD_SET(errfrom, &rd);
      FD_SET(from, &ex);
      FD_SET(errfrom, &ex);
      int s = ::select(max(from, errfrom)+1, &rd, 0, &ex, 0);
      if (FD_ISSET(from, &rd))
        {
          int r = read(from, output, size);
          out += std::string(output, r);
        }
      if (FD_ISSET(errfrom, &rd))
        {
          int r = read(errfrom, errout, size);
          err += std::string(errout, r);
        }
    } while (!stopped());
  int r = read(from, output, size);
  out += std::string(output, r);
  r = read(errfrom, errout, size);
  err += std::string(errout, r);
  delete[] output;
  delete[] errout;
}

void
monotone::inventory(std::vector<inventory_item> & out)
{
  out.clear();
  std::vector<std::string> args;
  std::string res = command("inventory", args);
  std::map<int, int> renames;
  int begin = 0;
  int end = res.find('\n', begin);
  while (begin != res.size())
    {
      int sp1 = begin + 4;
      int sp2 = res.find(' ', sp1 + 1);
      int sp3 = res.find(' ', sp2 + 1);
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
      end = res.find('\n', begin);
    }
}
std::vector<cert>
monotone::certs(std::string const & rev)
{
  std::vector<std::string> args;
  std::vector<cert> out;
  args.push_back(rev);
  std::string res = command("certs", args);
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
  return out;
}
std::vector<std::string>
monotone::select(std::string const & sel)
{
  std::vector<std::string> args, out;
  args.push_back(sel);
  std::string res = command("select", args);
  int begin = 0;
  int end = res.find('\n', begin);
  while (begin != res.size())
    {
      out.push_back(res.substr(begin, end-begin));
      begin = end + 1;
      end = res.find('\n', begin);
    }
  return out;
}
void
monotone::make_cert(std::string const & rev,
                std::string const & name,
                std::string const & value)
{
  std::vector<std::string> args;
  args.push_back(rev);
  args.push_back(name);
  args.push_back(value);
  std::string ign1, ign2;
  runcmd("cert", args, ign1, ign2);
}
std::string
monotone::commit(std::vector<std::string> args)
{
  std::string ign1, ign2;
  runcmd("commit", args, ign1, ign2);
  args.clear();
  std::string res = command("get_revision", args);
  for (int begin = 0, end = res.find('\n'); begin != res.size();
        begin = end + 1, end = res.find('\n', begin))
    {
      std::string line = res.substr(begin, end-begin);
      int lpos = line.find_first_of("[\"");
      int rpos = line.find_first_of("]\"", lpos + 1);
      std::string contents = line.substr(lpos + 1, rpos - lpos - 1);
      if (line.find("old_revision") < lpos)
        return contents;
    }
  return std::string();
}
std::string
monotone::diff(std::string const & filename)
{
  std::string out, ign;
  std::vector<std::string> args;
  args.push_back(filename);
  runcmd("diff", args, out, ign);
  return out;
}
std::string
monotone::diff(std::string const & filename,
                  std::string const & rev1,
                  std::string const & rev2)
{
  std::vector<std::string> args;
  args.push_back(filename);
  args.push_back("--revision=" + rev1);
  args.push_back("--revision=" + rev2);
  std::string out, ign;
  runcmd("diff", args, out, ign);
  return out;
}
std::string
monotone::cat(std::string const & filename, std::string const & rev)
{
  std::vector<std::string> args;
  args.push_back(filename);
  args.push_back("--revision=" + rev);
  std::string out, ign;
  runcmd("cat", args, out, ign);
  return out;
}
std::string
monotone::get_revision(std::string const & rev)
{
  std::vector<std::string> args;
  args.push_back(rev);
  return command("get_revision", args);
}
std::string
monotone::get_manifest(std::string const & rev)
{
  std::vector<std::string> args;
  args.push_back(rev);
  return command("get_manifest", args);
}
void
monotone::add(std::string const & file)
{
  std::string ign1, ign2;
  std::vector<std::string> args;
  args.push_back(file);
  runcmd("add", args, ign1, ign2);
}
void
monotone::drop(std::string const & file)
{
  std::string ign1, ign2;
  std::vector<std::string> args;
  args.push_back(file);
  runcmd("drop", args, ign1, ign2);
}
void
monotone::revert(std::string const & file)
{
  std::string ign1, ign2;
  std::vector<std::string> args;
  args.push_back(file);
  runcmd("revert", args, ign1, ign2);
}
void
monotone::rename(std::string const & oldname, std::string const & newname)
{
  std::string ign1, ign2;
  std::vector<std::string> args;
  args.push_back("--execute");
  args.push_back(oldname);
  args.push_back(newname);
  runcmd("rename", args, ign1, ign2);
}

bool
monotone::update(std::vector<std::string> & opts)
{
  opts.clear();
  std::string ign, err;
  std::vector<std::string> args;
  runcmd("update", args, ign, err);
  int p = err.find("multiple update candidates");
  if (p == std::string::npos)
    return true;

  p = err.find("monotone:   ", p + 1);
  while (p != std::string::npos)
    {
      p = err.find(":", p);
      p = err.find_first_not_of(" ", p + 1);
      opts.push_back(err.substr(p, 40));
      p = err.find("monotone:   ", p + 1);
    }
  return false;
}

void
monotone::update(std::string const & rev)
{
  std::string ign, err;
  std::vector<std::string> args;
  args.push_back("--revision="+rev);
  runcmd("update", args, ign, err);
}
