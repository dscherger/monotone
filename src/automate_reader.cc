// Copyright (C) 2005 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#include "base.hh"
#include "automate_reader.hh"

#include <iostream>

#include "sanity.hh"

using std::istream;
using std::pair;
using std::streamsize;
using std::string;
using std::vector;

bool automate_reader::get_string(string & out)
{
  out.clear();
  if (loc == none || loc == eof)
    {
      return false;
    }
  size_t size(0);
  char c;
  read(&c, 1);
  if (c == 'e')
    {
      loc = none;
      return false;
    }
  while(c <= '9' && c >= '0')
    {
      size = (size*10)+(c-'0');
      read(&c, 1);
    }
  E(c == ':', origin::user,
    F("Bad input to automate stdio: expected ':' after string size"));
  char *str = new char[size];
  size_t got = 0;
  while(got < size)
    {
      int n = read(str+got, size-got);
      got += n;
    }
  out = string(str, size);
  delete[] str;
  L(FL("Got string '%s'") % out);
  return true;
}
streamsize automate_reader::read(char *buf, size_t nbytes, bool eof_ok)
{
  streamsize rv;

  rv = in.rdbuf()->sgetn(buf, nbytes);

  E(eof_ok || rv > 0, origin::user,
    F("Bad input to automate stdio: unexpected EOF"));
  return rv;
}
void automate_reader::go_to_next_item()
{
  if (loc == eof)
    return;
  string starters("ol");
  string whitespace(" \r\n\t");
  string foo;
  while (loc != none)
    get_string(foo);
  char c('e');
  do
    {
      if (read(&c, 1, true) == 0)
        {
          loc = eof;
          return;
        }
    }
  while (whitespace.find(c) != string::npos);
  switch (c)
    {
    case 'o': loc = opt; break;
    case 'l': loc = cmd; break;
    default:
      E(false, origin::user,
        F("Bad input to automate stdio: unknown start token '%c'") % c);
    }
}
automate_reader::automate_reader(istream & is) : in(is), loc(none)
{}
bool automate_reader::get_command(vector<pair<string, string> > & params,
                                  vector<string> & cmdline)
{
  params.clear();
  cmdline.clear();
  if (loc == none)
    go_to_next_item();
  if (loc == eof)
    return false;
  else if (loc == opt)
    {
      string key, val;
      while(get_string(key) && get_string(val))
        params.push_back(make_pair(key, val));
      go_to_next_item();
    }
  E(loc == cmd, origin::user,
    F("Bad input to automate stdio: expected '%c' token") % cmd);
  string item;
  while (get_string(item))
    {
      cmdline.push_back(item);
    }
  E(cmdline.size() > 0, origin::user,
    F("Bad input to automate stdio: command name is missing"));
  return true;
}
void automate_reader::reset()
{
  loc = none;
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
