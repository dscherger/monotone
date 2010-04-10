// Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "simplestring_xform.hh"
#include "sanity.hh"
#include "constants.hh"

#include <set>
#include <algorithm>
#include <sstream>
#include <iterator>

using std::set;
using std::string;
using std::vector;
using std::ostringstream;
using std::ostream_iterator;
using std::transform;

struct
lowerize
{
  char operator()(unsigned char const & c) const
  {
    return ::tolower(static_cast<int>(c));
  }
};

string
lowercase(string const & in)
{
  string n(in);
  transform(n.begin(), n.end(), n.begin(), lowerize());
  return n;
}

struct
upperize
{
  char operator()(unsigned char const & c) const
  {
    return ::toupper(static_cast<int>(c));
  }
};

string
uppercase(string const & in)
{
  string n(in);
  transform(n.begin(), n.end(), n.begin(), upperize());
  return n;
}

void split_into_lines(string const & in,
                      vector<string> & out,
                      bool diff_compat)
{
  return split_into_lines(in, constants::default_encoding, out, diff_compat);
}

void split_into_lines(string const & in,
                      string const & encoding,
                      vector<string> & out)
{
  return split_into_lines(in, encoding, out, false);
}

void split_into_lines(string const & in,
                      string const & encoding,
                      vector<string> & out,
                      bool diff_compat)
{
  string lc_encoding = lowercase(encoding);
  out.clear();

  // note: this function does not handle ISO-2022-X, Shift-JIS, and
  // probably a good deal of other encodings as well. please expand
  // the logic here if you can work out an easy way of doing line
  // breaking on these encodings. currently it's just designed to
  // work with charsets in which 0x0a / 0x0d are *always* \n and \r
  // respectively.
  //
  // as far as I know, this covers the EUC, ISO-8859-X, GB, Big5, KOI,
  // ASCII, and UTF-8 families of encodings.

  if (lc_encoding == constants::default_encoding
      || lc_encoding.find("ascii") != string::npos
      || lc_encoding.find("8859") != string::npos
      || lc_encoding.find("euc") != string::npos
      || lc_encoding.find("koi") != string::npos
      || lc_encoding.find("gb") != string::npos
      || lc_encoding == "utf-8"
      || lc_encoding == "utf_8"
      || lc_encoding == "utf8")
    {
      string::size_type begin = 0;
      string::size_type end = in.find_first_of("\r\n", begin);

      while (end != string::npos && end >= begin)
        {
          out.push_back(in.substr(begin, end-begin));
          if (in.at(end) == '\r'
              && in.size() > end+1
              && in.at(end+1) == '\n')
            begin = end + 2;
          else
            begin = end + 1;
          if (begin >= in.size())
            break;
          end = in.find_first_of("\r\n", begin);
        }
      if (begin < in.size()) {
        // special case: last line without trailing newline
        string s = in.substr(begin, in.size() - begin);
        if (diff_compat) {
          // special handling: produce diff(1) compatible output
          s += (in.find_first_of("\r") != string::npos ? "\r\n" : "\n");
          s += "\\ No newline at end of file";
        }
        out.push_back(s);
      }
    }
  else
    {
      out.push_back(in);
    }
}


void
split_into_lines(string const & in,
                 vector<string> & out)
{
  split_into_lines(in, constants::default_encoding, out);
}

void
join_lines(vector<string> const & in,
           string & out,
           string const & linesep)
{
  join_lines(in.begin(), in.end(), out, linesep);
}

void
join_lines(vector<string>::const_iterator begin,
           vector<string>::const_iterator end,
           string & out,
           string const & linesep)
{
  ostringstream oss;
  copy(begin, end, ostream_iterator<string>(oss, linesep.c_str()));
  out = oss.str();
}

void
prefix_lines_with(string const & prefix, string const & lines, string & out)
{
  vector<string> msgs;
  split_into_lines(lines, msgs);

  ostringstream oss;
  for (vector<string>::const_iterator i = msgs.begin();
       i != msgs.end();)
    {
      oss << prefix << *i;
      i++;
      if (i != msgs.end())
        oss << '\n';
    }

  out = oss.str();
}

void
append_without_ws(string & appendto, string const & s)
{
  unsigned pos = appendto.size();
  appendto.resize(pos + s.size());
  for (string::const_iterator i = s.begin();
       i != s.end(); ++i)
    {
      switch (*i)
        {
        case '\n':
        case '\r':
        case '\t':
        case ' ':
          break;
        default:
          appendto[pos] = *i;
          ++pos;
          break;
        }
    }
  appendto.resize(pos);
}

string
remove_ws(string const & s)
{
  string tmp;
  append_without_ws(tmp, s);
  return tmp;
}

string
trim_left(string const & s, string const & chars)
{
  string tmp = s;
  string::size_type pos = tmp.find_first_not_of(chars);
  if (pos < string::npos)
    tmp = tmp.substr(pos);

  // if the first character in the string is still one of the specified
  // characters then the entire string is made up of these characters

  pos = tmp.find_first_of(chars);
  if (pos == 0)
    tmp = "";

  return tmp;
}

string
trim_right(string const & s, string const & chars)
{
  string tmp = s;
  string::size_type pos = tmp.find_last_not_of(chars);
  if (pos < string::npos)
    tmp.erase(++pos);

  // if the last character in the string is still one of the specified
  // characters then the entire string is made up of these characters

  pos = tmp.find_last_of(chars);
  if (pos == tmp.size()-1)
    tmp = "";

  return tmp;
}

string
trim(string const & s, string const & chars)
{
  string tmp = s;
  string::size_type pos = tmp.find_last_not_of(chars);
  if (pos < string::npos)
    tmp.erase(++pos);
  pos = tmp.find_first_not_of(chars);
  if (pos < string::npos)
    tmp = tmp.substr(pos);

  // if the first character in the string is still one of the specified
  // characters then the entire string is made up of these characters

  pos = tmp.find_first_of(chars);
  if (pos == 0)
    tmp = "";

  return tmp;
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
