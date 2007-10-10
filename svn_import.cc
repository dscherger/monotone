// Portions Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
// Portions Copyright (C) 2007 Markus Schiltknecht <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
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
          size_t & col,
          size_t & charpos)
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
          ++charpos;
          ist.get(c);
          return TOK_NEWLINE;
        }

      ++col;
      ++charpos;
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
      ++charpos;
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
          ++charpos;
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


struct svn_dump_parser
{
  istream & ist;
  string token;
  svn_token_type ttype;
  size_t line, col, charpos;

  int svn_dump_version;
  string svn_uuid;

  svn_dump_parser(istream & s)
    : ist(s), line(1), col(1), charpos(0)
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
    ttype = get_token(ist, token, line, col, charpos);
    // L(FL("token type %s: '%s'") % tt2str(ttype) % token);
  }

  void eat_raw_data(string & s, const size_t count)
  {
    // this is a little ugly, because we may already have a token in
    // the buffer, so we must prepend that or serve only parts of it.
    int rest = count - token.length();
    if (rest == 0)
      {
        s = token;
        advance();
      }
    else if (rest > 0)
      {
        char buf[rest];
        ist.read(buf, rest);
        charpos += count;
        col += 1;
        s = token + string(buf, rest);

        advance();
      }
    else
      {
        s = token.substr(0, count);
        token = token.substr(count);
      }
  }

  bool eof() { return ttype == TOK_NONE; }
  bool nump() { return ttype == TOK_NUM; }
  bool strp() { return ttype == TOK_STRING; }
  bool newlinep() { return ttype == TOK_NEWLINE; }
  bool strp(string const & val)
  {
    return ttype == TOK_STRING && token == val;
  }
  void eat(svn_token_type want)
  {
    if (ttype != want)
      throw oops((F("parse failure %d:%d: expecting %s, got %s with value '%s'")
          % line % col % tt2str(want) % tt2str(ttype) % token).str());
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

  void parse_int_field(const string exp, int & dest)
  {
    expect(exp);
    colon();
    num(dest);
    newline();
  }

  void parse_str_field(const string exp, string & dest)
  {
    expect(exp);
    colon();
    str(dest);
    newline();
  }

  void parse_properties()
  {
    int prop_content_length = 0, text_content_length = 0, content_length = 0;
    string text_delta, text_content_md5;

    while (1)
      {
        if (token == "Prop-content-length")
          parse_int_field("Prop-content-length", prop_content_length);

        else if (token == "Text-content-length")
          parse_int_field("Text-content-length", text_content_length);

        else if (token == "Text-content-md5")
          parse_str_field("Text-content-md5", text_content_md5);

        else if (token == "Text-delta")
          parse_str_field("Text-delta", text_delta);

        else if (token == "Content-length")
          {
            parse_int_field("Content-length", content_length);
            break;
          }
        else
          N(false, F("unknown properties header field"));
      }
    I(prop_content_length + text_content_length == content_length);

    if (prop_content_length > 0)
      {
        int prop_start = charpos;

        newline();

        while (((int) charpos - prop_start) < prop_content_length)
          {
            int key_len, value_len;
            string key, value;

            expect("K");
            num(key_len);
            newline();
            eat_raw_data(key, key_len);
            newline();
            expect("V");
            num(value_len);
            newline();
            eat_raw_data(value, value_len);
            newline();

            L(FL("    '%s': '%s'") % key % value);
  
            if (strp() && (token == "PROPS-END"))
              {
                I((int) charpos > prop_start);
                if (((int) charpos - prop_start) == prop_content_length)
                  {
                    L(FL("warning: charpos - prop_start = %d") % (charpos - prop_start));
                    L(FL("         prop_content_length = %d") % prop_content_length);
                  }
                break;
              }
          }

        expect("PROPS-END");

        while (newlinep())
          eat(TOK_NEWLINE);
      }

      if (text_content_length > 0)
        {
          string file_contents;
          eat_raw_data(file_contents, text_content_length);
        }

      while (newlinep())
        eat(TOK_NEWLINE);
  }

  void parse_header()
  {
    parse_int_field("SVN-fs-dump-format-version", svn_dump_version);

    L(FL("svn_dump_version: %d") % svn_dump_version);

    // svn dump format version 3 uses a special binary delta format
    // called svndelta. See subversion source code in file:
    // subversion/libsvn_delta/svndiff.c
    //
    // I'm not eager to add support for that format, as svnadmin dump
    // still supports both variants. And thanks to unix pipes, space
    // is not much of an issue.
    if (svn_dump_version == 3)
      throw oops((F("unable to import delta dumps (i.e. format version 3"
                    ).str()));

    if (svn_dump_version != 2)
      throw oops((F("unable to parse dump format version %d")
          % svn_dump_version).str());

    while (newlinep())
        eat(TOK_NEWLINE);

    parse_str_field("UUID", svn_uuid);
    L(FL("uuid: %s") % svn_uuid);

    while (newlinep())
        eat(TOK_NEWLINE);
  }

  void parse_revision()
  {
    int rev_nr;

    // To prevent an overrun of the chars counter, we restart
    // counting for every revision.
    charpos = 0;

    parse_int_field("Revision-number", rev_nr);
    L(FL("subversion revision %d") % rev_nr);
    parse_properties();

    while (strp() && token == "Node-path")
      {
        string path, kind, action;
        parse_str_field("Node-path", path);
        L(FL("  node path: %s") % path);

        parse_str_field("Node-kind", kind);
        I((kind == "dir") || (kind == "file"));
        L(FL("  node kind: %s") % kind);

        parse_str_field("Node-action", action);
        L(FL("  node action: %s") % action);
        parse_properties();
      }
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

  svn_dump_parser p(ist);
  while (!p.eof())
    {
      p.parse_revision();
    }
};

