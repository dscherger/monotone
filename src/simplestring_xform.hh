// Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __SIMPLESTRING_XFORM_HH__
#define __SIMPLESTRING_XFORM_HH__

#include "vector.hh"

std::string uppercase(std::string const & in);
std::string lowercase(std::string const & in);

void split_into_lines(std::string const & in,
                      std::vector<std::string> & out);

void split_into_lines(std::string const & in,
                      std::string const & encoding,
                      std::vector<std::string> & out);

void split_into_lines(std::string const & in,
                      std::vector<std::string> & out,
                      bool diff_compat);

void split_into_lines(std::string const & in,
                      std::string const & encoding,
                      std::vector<std::string> & out,
                      bool diff_compat);

void join_lines(std::vector<std::string> const & in,
                std::string & out,
                std::string const & linesep = "\n");

void join_lines(std::vector<std::string>::const_iterator begin,
                std::vector<std::string>::const_iterator end,
                std::string & out,
                std::string const & linesep = "\n");

template<class Thing> inline
origin::type get_made_from(Thing const & thing)
{
  return thing.made_from;
}
template<> inline
origin::type get_made_from<std::string>(std::string const & thing)
{
  return origin::internal;
}
template<class Thing> inline
Thing from_string(std::string const & str, origin::type made_from)
{
  return Thing(str, made_from);
}
template<> inline
std::string from_string<std::string>(std::string const & str, origin::type made_from)
{
  return str;
}

template< class T >
std::vector< T > split_into_words(T const & in)
{
  std::string const & instr = in();
  std::vector< T > out;

  std::string::size_type begin = 0;
  std::string::size_type end = instr.find_first_of(" ", begin);

  while (end != std::string::npos && end >= begin)
    {
      out.push_back(from_string<T>(instr.substr(begin, end-begin),
                                   get_made_from(in)));
      begin = end + 1;
      if (begin >= instr.size())
        break;
      end = instr.find_first_of(" ", begin);
    }
  if (begin < instr.size())
    out.push_back(from_string<T>(instr.substr(begin, instr.size() - begin),
                                 get_made_from(in)));

  return out;
}

template< class Container >
typename Container::value_type join_words(Container const & in,
                                          std::string const & sep = " ")
{
  origin::type made_from = origin::internal;
  std::string str;
  typename Container::const_iterator iter = in.begin();
  while (iter != in.end())
    {
      made_from = get_made_from(*iter);
      str += (*iter)();
      iter++;
      if (iter != in.end())
        str += sep;
    }
  typedef typename Container::value_type result_type;
  return from_string<result_type>(str, made_from);
}

void prefix_lines_with(std::string const & prefix,
                       std::string const & lines,
                       std::string & out);

// append after removing all whitespace
void append_without_ws(std::string & appendto, std::string const & s);

// remove all whitespace
std::string remove_ws(std::string const & s);

// remove leading chars from string
std::string trim_left(std::string const & s,
                      std::string const & chars = "\n\r\t ");

// remove trailing chars from string
std::string trim_right(std::string const & s,
                       std::string const & chars = "\n\r\t ");

// remove leading and trailing chars from string
std::string trim(std::string const & s,
                 std::string const & chars = "\n\r\t ");

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
