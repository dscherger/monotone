// Copyright (C) 2007 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <sstream>
#include <cctype>
#include <cstdlib>

#include "json_io.hh"
#include "sanity.hh"
#include "vocab.hh"

using std::logic_error;
using std::make_pair;
using std::pair;
using std::string;
using std::vector;


void 
json_io::input_source::err(string const & s)
{
  E(false,
    F("parsing a %s at %d:%d:E: %s") % name % line % col % s);
}


void 
json_io::tokenizer::err(string const & s)
{
  in.err(s);
}

string json_io::printer::buf;
size_t json_io::printer::count;


json_io::printer::printer()
{  
  I(count == 0);
  count++;
  indent = 0;
  buf.clear();
}

json_io::printer::~printer()
{
  count--;
}

string
json_io::escape(string const & s)
{
  string escaped;
  escaped.reserve(s.size() + 8);

  escaped += "\"";

  for (string::const_iterator i = s.begin(); i != s.end(); ++i)
    {
      switch (*i)
        {
        case '"':
          escaped += "\\\"";
          break;
        case '\\':
          escaped += "\\\\";
          break;
        case '/':
          escaped += "\\/";
          break;
        case '\b':
          escaped += "\\b";
          break;
        case '\f':
          escaped += "\\f";
          break;
        case '\n':
          escaped += "\\n";
          break;
        case '\r':
          escaped += "\\r";
          break;
        case '\t':
          escaped += "\\t";
          break;          
        default:
          escaped += *i;
        }
    }

  escaped += "\"";

  return escaped;
}



void json_io::parser::err(string const & s)
{
  tok.err(s);
}

string json_io::parser::tt2str(token_type tt)
{
  switch (tt)
    {
    case json_io::TOK_STRING:
      return "TOK_STRING";
    case json_io::TOK_SYMBOL:
      return "TOK_SYMBOL";
    case json_io::TOK_LBRACE:
      return "TOK_LBRACE";
    case json_io::TOK_RBRACE:
      return "TOK_RBRACE";
    case json_io::TOK_LBRACKET:
      return "TOK_LBRACKET";
    case json_io::TOK_RBRACKET:
      return "TOK_RBRACKET";
    case json_io::TOK_COMMA:
      return "TOK_COMMA";
    case json_io::TOK_COLON:
      return "TOK_COLON";
    case json_io::TOK_NONE:
      return "TOK_NONE";
    }
  return "TOK_UNKNOWN";
}


void
json_io::json_string::write(printer &pr)
{
  pr.append(escape(data));
}

void
json_io::json_object::write(printer &pr)
{
  bool first = true;
  pr.append("{\n");
  pr.indent++;
  for (std::map<std::string, json_value_t>::const_iterator 
         i = fields.begin(); i != fields.end(); ++i) 
    {
      if (!first)
        pr.append(",\n");
      pr.append_indent();
      pr.append(escape(i->first));
      pr.append(": ");
      i->second->write(pr);
      first = false;
    }
  pr.indent--;
  pr.append("\n");
  pr.append_indent();
  pr.append("}");
}
  
void 
json_io::json_array::write(printer &pr)
{
  bool first = true;
  pr.append("[\n");
  pr.indent++;
  for (std::vector<json_value_t>::const_iterator 
         i = fields.begin(); i != fields.end(); ++i) 
    {
      if (!first)
        pr.append(",\n");
      pr.append_indent();
      (*i)->write(pr);
      first = false;
    }
  pr.indent--;
  pr.append("\n");
  pr.append_indent();
  pr.append("]");
}



#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

UNIT_TEST(json_io, binary_transparency)
{
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
