#ifndef __JSON_IO_HH__
#define __JSON_IO_HH__

// Copyright (C) 2007 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#include "vector.hh"
#include <map>

#include <boost/shared_ptr.hpp>

#include "char_classifiers.hh"
#include "paths.hh"
#include "sanity.hh"
#include "vocab.hh"
#include "numeric_vocab.hh"
#include "safe_map.hh"

namespace json_io
{

  ///////////////////////////////////////////////////////////
  // vocabulary
  ///////////////////////////////////////////////////////////

  struct printer;

  struct json_value
  {
    virtual void write(printer &pr) = 0;
    virtual ~json_value() {}
  };

  typedef boost::shared_ptr<json_value> json_value_t;

  struct json_object
    : public json_value
  {
    std::map<std::string, json_value_t> fields;
    void add(std::string const &str, json_value_t v)
    { fields.insert(std::make_pair(str, v)); }
    virtual void write(printer &pr);
    virtual ~json_object() {}
  };

  typedef boost::shared_ptr<json_object> json_object_t;

  struct json_array
    : public json_value
  {
    std::vector<json_value_t> fields;
    void add(json_value_t v)
    { fields.push_back(v); }
    virtual void write(printer &pr);
    virtual ~json_array() {}
  };

  typedef boost::shared_ptr<json_array> json_array_t;

  struct json_string
    : public json_value
  {
    json_string(std::string const &s) : data(s) {}
    std::string data;
    virtual void write(printer &pr);
    virtual ~json_string() {}
  };

  typedef boost::shared_ptr<json_string> json_string_t;

  ///////////////////////////////////////////////////////////
  // lexing
  ///////////////////////////////////////////////////////////

  typedef enum
    {
      TOK_SYMBOL,
      TOK_STRING,
      TOK_LBRACE,
      TOK_RBRACE,
      TOK_LBRACKET,
      TOK_RBRACKET,
      TOK_COMMA,
      TOK_COLON,
      TOK_NONE
    } token_type;

  struct
  input_source
  {
    size_t line, col;
    std::string const & in;
    std::string::const_iterator curr;
    std::string name;
    int lookahead;
    char c;
    input_source(std::string const & in, std::string const & nm)
      : line(1), col(1), in(in), curr(in.begin()),
        name(nm), lookahead(0), c('\0')
    {}

    inline void peek()
    {
      if (LIKELY(curr != in.end()))
        // we do want to distinguish between EOF and '\xff',
        // so we translate '\xff' to 255u
        lookahead = widen<unsigned int,char>(*curr);
      else
        lookahead = EOF;
    }

    inline void advance()
    {
      if (LIKELY(curr != in.end()))
        {
          c = *curr;
          ++curr;
          ++col;
          if (c == '\n')
            {
              col = 1;
              ++line;
            }
        }
      peek();
    }
    void err(std::string const & s);
  };

  struct
  tokenizer
  {
    input_source & in;
    std::string::const_iterator begin;
    std::string::const_iterator end;

    tokenizer(input_source & i) : in(i), begin(in.curr), end(in.curr)
    {}

    inline void mark()
    {
      begin = in.curr;
      end = begin;
    }

    inline void advance()
    {
      in.advance();
      end = in.curr;
    }

    inline void store(std::string & val)
    {
      val.assign(begin, end);
    }

    inline void
    read_escape(std::string & val, char c)
    {
      switch (c)
        {
        case '/':
        case '\\':
        case '"':
          val += c;
          break;
        case 'b':
          val += '\b';
          break;
        case 'f':
          val += '\f';
          break;
        case 'n':
          val += '\n';
          break;
        case 'r':
          val += '\r';
          break;
        case 't':
          val += '\t';
          break;
        default:
          in.err("unrecognized character escape");
          break;
        }
    }

    inline token_type get_token(std::string & val)
    {
      in.peek();

      while (true)
        {
          if (UNLIKELY(in.lookahead == EOF))
            return TOK_NONE;
          if (!is_space(in.lookahead))
            break;
          in.advance();
        }

      if (is_alpha(in.lookahead))
        {
          mark();
          while (is_alnum(in.lookahead) || in.lookahead == '_')
            advance();
          store(val);
          return json_io::TOK_SYMBOL;
        }

      else if (in.lookahead == '"')
        {
          in.advance();
          mark();
          while (static_cast<char>(in.lookahead) != '"')
            {
              if (UNLIKELY(in.lookahead == EOF))
                in.err("input stream ended in string");
              if (UNLIKELY(static_cast<char>(in.lookahead) == '\\'))
                {
                  // When we hit an escape, we switch from doing mark/store
                  // to a slower per-character append loop, until the end
                  // of the token.

                  // So first, store what we have *before* the escape.
                  store(val);

                  // Then skip over the escape backslash.
                  in.advance();

                  // Handle the escaped char.
                  read_escape(val, static_cast<char>(in.lookahead));

                  // Advance past the escaped char.
                  in.advance();

                  // Now enter special slow loop for remainder.
                  while (static_cast<char>(in.lookahead) != '"')
                    {
                      if (UNLIKELY(in.lookahead == EOF))
                        in.err("input stream ended in string");
                      if (UNLIKELY(static_cast<char>(in.lookahead) == '\\'))
                        {
                          in.advance();
                          read_escape(val, static_cast<char>(in.lookahead));
                          in.advance();
                        }
                      else
                        {
                          in.advance();
                          val += in.c;
                        }
                    }
                  // When slow loop completes, return early.
                  if (static_cast<char>(in.lookahead) != '"')
                    in.err("string did not end with '\"'");
                  in.advance();

                  return json_io::TOK_STRING;
                }
              advance();
            }

          store(val);

          if (UNLIKELY(static_cast<char>(in.lookahead) != '"'))
            in.err("string did not end with '\"'");
          in.advance();

          return json_io::TOK_STRING;
        }
      else if (in.lookahead == '[')
        {
          in.advance();
          return json_io::TOK_LBRACKET;
        }
      else if (in.lookahead == ']')
        {
          in.advance();
          return json_io::TOK_RBRACKET;
        }
      else if (in.lookahead == '{')
        {
          in.advance();
          return json_io::TOK_LBRACE;
        }
      else if (in.lookahead == '}')
        {
          in.advance();
          return json_io::TOK_RBRACE;
        }
      else if (in.lookahead == ':')
        {
          in.advance();
          return json_io::TOK_COLON;
        }
      else if (in.lookahead == ',')
        {
          in.advance();
          return json_io::TOK_COMMA;
        }
      else
        return json_io::TOK_NONE;
    }
   void err(std::string const & s);
  };


  ///////////////////////////////////////////////////////////
  // parsing
  ///////////////////////////////////////////////////////////

  struct
  parser
  {
    tokenizer & tok;
    parser(tokenizer & t) : tok(t)
    {
      token.reserve(128);
      advance();
    }

    std::string token;
    token_type ttype;

    void err(std::string const & s);
    std::string tt2str(token_type tt);

    inline void advance()
    {
      ttype = tok.get_token(token);
    }

    inline void eat(token_type want)
    {
      if (ttype != want)
        err("wanted "
            + tt2str(want)
            + ", got "
            + tt2str(ttype)
            + (token.empty()
               ? std::string("")
               : (std::string(" with value ") + token)));
      advance();
    }


    inline json_object_t
    parse_object()
    {
      bool first = true;
      json_object_t obj(new json_object());
      lbrace();
      while (ttype != TOK_RBRACE)
        {
          if (!first)
            comma();
          first = false;
          std::string key;
          str(key);
          colon();
          json_value_t val = parse_value();
          safe_insert(obj->fields, std::make_pair(key, val));
        }
      rbrace();
      return obj;
    }

    inline json_array_t
    parse_array()
    {
      bool first = true;
      json_array_t arr(new json_array());
      lbracket();
      while (ttype != TOK_RBRACKET)
        {
          if (!first)
            comma();
          first = false;
          json_value_t val = parse_value();
          arr->add(val);
        }
      rbracket();
      return arr;
    }

    inline json_string_t
    parse_string()
    {
      json_string_t s(new json_string(""));
      str(s->data);
      return s;
    }

    inline json_value_t
    parse_value()
    {
      if (ttype == TOK_LBRACE)
        return parse_object();
      else if (ttype == TOK_LBRACKET)
        return parse_array();
      else if (ttype == TOK_STRING)
        return parse_string();
      else
        return json_value_t();
    }

    inline void str() { eat(json_io::TOK_STRING); }
    inline void sym() { eat(json_io::TOK_SYMBOL); }
    inline void colon() { eat(json_io::TOK_COLON); }
    inline void comma() { eat(json_io::TOK_COMMA); }
    inline void lbrace() { eat(json_io::TOK_LBRACE); }
    inline void rbrace() { eat(json_io::TOK_RBRACE); }
    inline void lbracket() { eat(json_io::TOK_LBRACKET); }
    inline void rbracket() { eat(json_io::TOK_RBRACKET); }

    inline void str(std::string & v) { v = token; str(); }
    inline void sym(std::string & v) { v = token; sym(); }
  };


  ///////////////////////////////////////////////////////////
  // printing
  ///////////////////////////////////////////////////////////

  std::string escape(std::string const & s);

  // Note: printer uses a static buffer; thus only one buffer
  // may be referenced (globally). An invariant will be triggered
  // if more than one json_io::printer is instantiated.
  struct
  printer
  {
    static std::string buf;
    static size_t count;
    size_t indent;
    printer();
    ~printer();
    void append(std::string const &s)
    {
      buf.append(s);
    }
    void append_indent()
    {
      for (size_t i = 0; i < indent; ++i)
        buf += '\t';
    }
  };


  ///////////////////////////////////////////////////////////
  /////////////////////// building //////////////////////////
  ///////////////////////////////////////////////////////////

  struct builder
  {
    json_value_t v;
    std::string key;
    builder(json_value_t v, symbol const &k) : v(v), key(k()) {}
    builder(json_value_t v) : v(v), key("") {}
    builder() : v(new json_object()), key("") {}

    json_object_t as_obj()
    {
      json_object_t ob = boost::dynamic_pointer_cast<json_object, json_value>(v);
      I(static_cast<bool>(ob));
      return ob;
    }

    json_array_t as_arr()
    {
      json_array_t a = boost::dynamic_pointer_cast<json_array, json_value>(v);
      I(static_cast<bool>(a));
      return a;
    }

    json_string_t as_str()
    {
      json_string_t s = boost::dynamic_pointer_cast<json_string, json_value>(v);
      I(static_cast<bool>(s));
      return s;
    }

    builder bad()
    {
      return builder(json_value_t());
    }

    builder operator[](symbol const &k)
    {
      I(key.empty());
      return builder(as_obj(), k);
    }

    void add_str(std::string const &s)
    {
      I(key.empty());
      as_arr()->add(json_string_t(new json_string(s)));
    }

    builder add_obj()
    {
      I(key.empty());
      json_array_t a = as_arr();
      json_object_t ob(new json_object());
      a->add(ob);
      return builder(ob);
    }

    builder add_arr()
    {
      I(key.empty());
      json_array_t a = as_arr();
      json_array_t a2(new json_array());
      a->add(a2);
      return builder(a2);
    }

    void add(json_value_t val)
    {
      I(key.empty());
      as_arr()->add(val);
    }

    void set(json_value_t v)
    {
      I(!key.empty());
      as_obj()->add(key, v);
    }

    void str(std::string const &s)
    {
      set(json_string_t(new json_string(s)));
    }

    builder obj()
    {
      json_object_t ob(new json_object());
      set(ob);
      return builder(ob);
    }

    builder arr()
    {
      json_array_t a(new json_array());
      set(a);
      return builder(a);
    }
  };

  ///////////////////////////////////////////////////////////
  ///////////////////////   query  //////////////////////////
  ///////////////////////////////////////////////////////////

  struct
  query
  {
    json_value_t v;
    query(json_value_t v) : v(v) {}

    json_object_t as_obj()
    {
      return boost::dynamic_pointer_cast<json_object, json_value>(v);
    }

    json_array_t as_arr()
    {
      return boost::dynamic_pointer_cast<json_array, json_value>(v);
    }

    json_string_t as_str()
    {
      return boost::dynamic_pointer_cast<json_string, json_value>(v);
    }

    query bad()
    {
      return query(json_value_t());
    }


    query operator[](symbol const &key)
    {
      json_object_t ob = as_obj();
      if (static_cast<bool>(ob))
        {
          std::map<std::string, json_value_t>::const_iterator i = ob->fields.find(key());
          if (i != ob->fields.end())
            return query(i->second);
        }
      return bad();
    }

    query operator[](size_t &idx)
    {
      json_array_t a = as_arr();
      if (static_cast<bool>(a) && idx < a->fields.size())
        return query(a->fields.at(idx));
      return bad();
    }

    bool len(size_t & length) {
      json_array_t a = as_arr();
      if (static_cast<bool>(a)) {
        length = a->fields.size();
        return true;
      }
      return false;
    }

    json_value_t get()
    {
      return v;
    }

    bool get(std::string & str) {
      json_string_t s = as_str();
      if (static_cast<bool>(s))
        {
          str = s->data;
          return true;
        }
      return false;
    }
  };
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __JSON_IO_HH__
