// Portions Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
// Portions Copyright (C) 2007 Markus Schiltknecht <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "cert.hh"
#include "cmd.hh"
#include "app_state.hh"
#include "keys.hh"
#include "svn_import.hh"
#include "sanity.hh"

using std::string;
using std::istream;

typedef enum
  {
    TOK_NEWLINE,
    TOK_STRING,
    TOK_NUM,
    TOK_COLON,
    TOK_NONE
  }
svn_token_type;

static svn_token_type
get_token(istream & ist,
          string & str,
      	  size_t & line,
	        size_t & col)
{
  bool digits_only = true;
  int i = ist.peek();
  char c;
  str.clear();

  // eat leading whitespace
  while (true)
    {
      if (i == EOF)
        return TOK_NONE;

      if (i == '\n')
        {
          col = 0;
          ++line;
          ist.get(c);
          return TOK_NEWLINE;
        }

      ++col;
      if (!isspace(i))
        break;
      ist.get(c);
      i = ist.peek();
    }

  switch (i)
    {
    case ':':
      ist.get(c);
      ++col;
      return TOK_COLON;
      break;

    default:
      while (ist.good()
             && i != ':'
             && !isspace(i))
        {
          if (!isdigit(i))
            digits_only = false;
          ist.get(c);
          ++col;
          str += c;
          i = ist.peek();
        }
      break;
    }

  if (str.empty())
    return TOK_NONE;
  else
    if (digits_only)
      return TOK_NUM;
    else
      return TOK_STRING;
}


struct parser
{
  istream & ist;
  string token;
  svn_token_type ttype;
  size_t line, col;

  int svn_dump_version;

  parser(istream & s)
    : ist(s), line(1), col(1)
  {
    advance();
    parse_header();
  }

  string tt2str(svn_token_type tt)
  {
    switch (tt)
      {
      case TOK_STRING:
        return "TOK_STRING";
      case TOK_NUM:
        return "TOK_NUM";
      case TOK_NEWLINE:
        return "TOK_NEWLINE";
      case TOK_COLON:
        return "TOK_COLON";
      case TOK_NONE:
        return "TOK_NONE";
      }
    return "TOK_UNKNOWN";
  }

  void advance()
  {
    ttype = get_token(ist, token, line, col);
    L(FL("token type %s: '%s'") % tt2str(ttype) % token);
  }

  bool nump() { return ttype == TOK_NUM; }
  bool strp() { return ttype == TOK_STRING; }
  bool strp(string const & val)
  {
    return ttype == TOK_STRING && token == val;
  }
  void eat(svn_token_type want)
  {
    if (ttype != want)
      {
      throw oops((F("parse failure %d:%d: expecting %s, got %s with value '%s'")
		  % line % col % tt2str(want) % tt2str(ttype) % token).str());
      }
    advance();
  }

  // basic "expect / extract" functions

  void str(string & v) { v = token; eat(TOK_STRING); }
  void str() { eat(TOK_STRING); }
  void num(int & v) { v = atoi(token.c_str()); eat(TOK_NUM); }
  void num() { eat(TOK_NUM); }
  void colon() { eat(TOK_COLON); }
  void newline() { eat(TOK_NEWLINE); }
  void expect(string const & expected)
  {
    string tmp;
    if (!strp(expected))
      throw oops((F("parse failure %d:%d: expecting word '%s'")
		  % line % col % expected).str());
    advance();
  }

  void parse_header()
  {
    expect("SVN-fs-dump-format-version");
    colon();
    num(svn_dump_version);
    L(FL("svn_dump_version: %d") % svn_dump_version);
    newline();

    if ((svn_dump_version < 2) or (svn_dump_version > 3))
      throw oops((F("unable to parse dump format version %d")
		  % svn_dump_version).str());
  }

  void parse_record()
  {
    // at the end, we need a TOK_NONE
    eat(TOK_NONE);
  }
};

void
import_svn_repo(istream & ist, app_state & app)
{
  // early short-circuit to avoid failure after lots of work
  rsa_keypair_id key;
  get_user_key(key, app);
  require_password(key, app);

  N(app.opts.branchname() != "", F("need base --branch argument for importing"));

  string branch = app.opts.branchname();

  parser p(ist);
};
