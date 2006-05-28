#include <iostream>
#include <sstream>
#include <string>
#include <cctype>
#include <cstdlib>

#include <boost/lexical_cast.hpp>

#include "basic_io.hh"
#include "sanity.hh"
#include "vocab.hh"

using std::logic_error;
using std::make_pair;
using std::pair;
using std::string;
using std::vector;

// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this file provides parsing and printing primitives used by the higher
// level parser and printer routines for the two datatypes change_set and
// revision_set. every revision_set contains a number of change_sets, so
// their i/o routines are somewhat related.


void basic_io::input_source::err(string const & s)
{
  L(FL("error in %s:%d:%d:E: %s") % name % line % col % s);
  throw logic_error((F("error in %s:%d:%d:E: %s") 
                     % name % line % col % s).str());
}


void basic_io::tokenizer::err(string const & s)
{
  in.err(s);
}

string 
basic_io::escape(string const & s)
{
  string escaped;
  escaped.reserve(s.size() + 8);

  escaped += "\"";

  for (string::const_iterator i = s.begin(); i != s.end(); ++i)
    {
      switch (*i)
        {
        case '\\':
        case '"':
          escaped += '\\';
        default:
          escaped += *i;
        }
    }

  escaped += "\"";

  return escaped;
}

basic_io::stanza::stanza() : indent(0)
{}

void basic_io::stanza::push_hex_pair(symbol const & k, hexenc<id> const & v)
{
  entries.push_back(make_pair(k, "[" + v() + "]"));
  if (k().size() > indent)
    indent = k().size();
}

void basic_io::stanza::push_hex_triple(symbol const & k, 
				       string const & n, 
				       hexenc<id> const & v)
{
  entries.push_back(make_pair(k, escape(n) + " " + "[" + v() + "]"));
  if (k().size() > indent)
    indent = k().size();
}

void basic_io::stanza::push_str_pair(symbol const & k, string const & v)
{
  entries.push_back(make_pair(k, escape(v)));
  if (k().size() > indent)
    indent = k().size();
}

void basic_io::stanza::push_file_pair(symbol const & k, file_path const & v)
{
  push_str_pair(k, v.as_internal());
}

void basic_io::stanza::push_str_multi(symbol const & k,
                                      vector<string> const & v)
{
  string val;
  bool first = true;
  for (vector<string>::const_iterator i = v.begin();
       i != v.end(); ++i)
    {
      if (!first)
        val += " ";
      val += escape(*i);
      first = false;
    }
  entries.push_back(make_pair(k, val));
  if (k().size() > indent)
    indent = k().size();
}

void basic_io::stanza::push_str_triple(symbol const & k, 
                                       string const & n,
                                       string const & v)
{
  entries.push_back(make_pair(k, escape(n) + " " + escape(v)));
  if (k().size() > indent)
    indent = k().size();
}


string basic_io::printer::buf;

basic_io::printer::printer() 
{
  buf.clear();
}

void basic_io::printer::print_stanza(stanza const & st)
{
  if (LIKELY(!buf.empty()))
    buf += '\n';

  for (vector<pair<symbol, string> >::const_iterator i = st.entries.begin();
       i != st.entries.end(); ++i)
    {
      for (size_t k = i->first().size(); k < st.indent; ++k)
        buf += ' ';
      buf.append(i->first());
      buf += ' ';
      buf.append(i->second);
      buf += '\n';
    }
}

void basic_io::parser::err(string const & s)
{
  tok.err(s);
}

string basic_io::parser::tt2str(token_type tt)
{
  switch (tt)
    {
    case basic_io::TOK_STRING:
      return "TOK_STRING";
    case basic_io::TOK_SYMBOL:
      return "TOK_SYMBOL";
    case basic_io::TOK_HEX:
      return "TOK_HEX";
    case basic_io::TOK_NONE:
      return "TOK_NONE";
    }
  return "TOK_UNKNOWN";
}


