// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "ignore_set.hh"
#include "file_io.hh"
#include "pcrewrap.hh"
#include "vector.hh"
#include <map>
#include "safe_map.hh"
#include "simplestring_xform.hh"

using std::vector;
using std::string;
using std::map;

// wrapper logic

typedef vector<pcre::regex> re_set;

struct ignore_set_impl
{
  re_set in;
  re_set out;

  ignore_set_impl();
  bool included(file_path const &);
};

ignore_set::~ignore_set()
{
  if (imp)
    delete imp;
}

bool
ignore_set::included(file_path const & path)
{
  if (!imp)
    imp = new ignore_set_impl();
  return imp->included(path);
}


// the syntax of .mtn-ignore is as follows:
//
// any trailing '\n' and/or '\r' is stripped from each input line, as are
// all leading and trailing ' ' and/or '\t' characters.  after this is done:
//
// empty lines are ignored.
// lines beginning with '#' are comments, and are ignored.
// lines beginning with '!' contribute to the not-ignore list rather than
// the ignore list.
//
// after # and ! processing, we look for leading or trailing slashes.
// they are replaced by constructs that cause a leading / to
// match at any directory boundary including the root, and a trailing / to
// match both the contents of a directory and the directory itself.

static void
parse_one_ignore_line(string const & orig_line,
                      map<string, pcre::regex> & in_pats,
                      map<string, pcre::regex> & out_pats,
                      char const *filename, int lineno)
{
  typedef string::size_type st;

  st beg = orig_line.find_first_not_of(" \t");
  st end = orig_line.find_last_not_of(" \t\r\n");

  if (beg == string::npos || end == string::npos)
    return;    // line is empty or entirely whitespace
  if (orig_line[beg] == '#')
    return;    // comment

  string line(orig_line, beg, end - beg + 1);
  I(!line.empty());

  bool this_one_in = true;
  if (line[0] == '!') // don't-ignore regex
    {
      line.erase(0,1);
      this_one_in = false;
      if (line.empty())
        {
          W(F("on line %d of %s: empty don't-ignore regex")
            % lineno % filename);
          return;
        }
    }

  if (line[0] == '/')
    {
      if (line.size() == 1)
        {
          W(F("on line %d of %s: lone \"/\" can't match anything")
            % lineno % filename);
          return;
        }
      line.replace(0, 1, "(?:/|^)");
    }

  if (line[line.size()-1] == '/')
    line.replace(line.size() - 1, 1, "(?:/|$)");

  try
    {
      pcre::regex pat(line);

      map<string, pcre::regex>::iterator x = in_pats.find(line);
      if (this_one_in)
        {
          if (x != in_pats.end())
            {
              W(F("on line %d of %s: duplicate regex \"%s\"")
                % lineno % filename % orig_line);
              return;
            }
          safe_insert(in_pats, make_pair(line, pat));
        }
      else
        {
          if (x != in_pats.end())
            in_pats.erase(x);
          else
            {
              map<string, pcre::regex>::iterator y = out_pats.find(line);
              if (y != out_pats.end())
               {
                 W(F("on line %d of %s: duplicate regex \"%s\"")
                   % lineno % filename % orig_line);
                 return;
               }
              safe_insert(out_pats, make_pair(line, pat));
            }
        }
    }
  catch (informative_failure & e)
    {
      W(F("on line %d of %s: %s")
        % lineno % filename % e.what());
      return;
    }
}

// this array comprises the default set of filename patterns to be ignored
// if unknown.  it is an array rather than a pre-optimized regular
// expression for three reasons: first, it's easier to edit that way;
// second, as an optimization, if prepare_ignore_regexps sees a not-ignore
// pattern that exactly matches one of these, it will drop it from the set
// of to-ignore patterns rather than add to the not-ignore set; and third,
// there is a command that prints out this list as if it were an ignore
// file.  (this last is why there are comments embedded in the array.)
//
// note that this array is run through the same parser as .mtn-ignore; this
// is necessary to handle comments and directory patterns.  note also that
// we explicitly escape all non-metacharacter punctuation, as a precaution.

static char const * const default_ignores[] = {
  "# c/c++",
  "\\.a$",
  "\\.so$",
  "\\.o$",
  "\\.la$",
  "\\.lo$",
  "/core$",
  "/core\\.\\d+$",
  "# java",
  "\\.class$",
  "# python",
  "\\.pyc$",
  "\\.pyo$",
  "# gettext",
  "\\.g?mo$",
  "# intltool",
  "\\.intltool\\-merge\\-cache$",
  "# TeX",
  "\\.aux$",
  "# backup files",
  "\\.bak$",
  "\\.orig$",
  "\\.rej$",
  "\\~$",
  "# vim creates .foo.swp files",
  "\\.[^/]*\\.swp$",
  "# emacs creates #foo# files",
  "/\\#[^/]*\\#$",
  "# other VCSes (where metadata is stored in named files):",
  "\\.scc$",
  "# desktop/directory configuration metadata",
  "/\\.DS_Store$",
  "/desktop\\.ini$",
  "# autotools detritus",
  "/autom4te\\.cache/",
  "/\\.deps/",
  "/\\.libs/",
  "# Cons/SCons detritus",
  "/\\.consign/",
  "/\\.sconsign/",
  "# other VCSes (where metadata is stored in named dirs):",
  "/CVS/",
  "/\\.svn/",
  "/SCCS/",
  "/_darcs/",
  "/\\.cdv/",
  "/\\.git/",
  "/\\.bzr/",
  "/\\.hg/",
  0
};

ignore_set_impl::ignore_set_impl()
{
  map<string, pcre::regex> in_pats;
  map<string, pcre::regex> out_pats;
  
  // read defaults
  // parse_one_ignore_line should never give a diagnostic for these, so
  // we don't worry about translating the fake file tag we use
  for (size_t i = 0; default_ignores[i]; i++)
    parse_one_ignore_line(default_ignores[i], in_pats, out_pats,
                          "<built-in>", i+1);

  // read .mtn-ignore if it exists
  file_path ignorefile = file_path_internal(".mtn-ignore");
  if (file_exists(ignorefile))
    {
      data ignorefile_dat;
      vector<string> ignorefile_vec;
      size_t lineno = 1;

      read_data(ignorefile, ignorefile_dat);
      split_into_lines(ignorefile_dat(), ignorefile_vec);
      for (vector<string>::const_iterator i = ignorefile_vec.begin();
           i != ignorefile_vec.end(); i++, lineno++)
        parse_one_ignore_line(*i, in_pats, out_pats, ".mtn-ignore", lineno);
    }

  // now copy the compiled regexes into permanent storage
  in.reserve(in_pats.size());
  out.reserve(out_pats.size());
  for (map<string, pcre::regex>::const_iterator i = in_pats.begin();
       i != in_pats.end(); i++)
    in.push_back(i->second);

  for (map<string, pcre::regex>::const_iterator i = out_pats.begin();
       i != out_pats.end(); i++)
    out.push_back(i->second);
}

bool
ignore_set_impl::included(file_path const & path)
{
  for (re_set::const_iterator i = in.begin(); i != in.end(); i++)
    if (i->match(path.as_internal()))
      goto maybe_matched;

  return false;

 maybe_matched:
  for (re_set::const_iterator i = out.begin(); i != out.end(); i++)
    if (i->match(path.as_internal()))
      return false;

  return true;
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include <boost/current_function.hpp>

#define PARSE_ONE(T, I, O) \
  parse_one_ignore_line(T, I, O, BOOST_CURRENT_FUNCTION, __LINE__)
#define PARSE_ONE_LINENO(T, I, O, L) \
  parse_one_ignore_line(T, I, O, BOOST_CURRENT_FUNCTION, L)


UNIT_TEST(ignore, line_parsing)
{
  struct single_line_case
  {
    char const * str;
    char const * exp_in;
    char const * exp_out;
  };
  single_line_case const tcases[] = {
    // commentary
    { "", 0, 0 },
    { "#qwertyuiop!@#$%^&*()_+", 0, 0 },
    { "#! /bin /sh", 0, 0 },
    { " \t\n\r", 0, 0 },
    { " \r\t\n", 0, 0 },
    { " #fnord", 0, 0 },

    // things which are not comments but are still ignored
    { "/", 0, 0 },
    { "!", 0, 0 },
    { "!/", 0, 0 },

    // whitespace stripping
    { "abc",     "abc", 0 },
    { "  abc",   "abc", 0 },
    { "abc   ",  "abc", 0 },
    { " abc \t", "abc", 0 },
    { "abc\r\n", "abc", 0 },
    { "\rabc",   "\rabc", 0 }, // impossible in real usage but.

    // interior whitespace is preserved
    { "a b c d e f", "a b c d e f", 0 },

    // leading punctuation
    { "\\.foo", "\\.foo", 0 },
    { "^foo", "^foo", 0 },
    { "[ab]cd", "[ab]cd", 0 },
    { "(foo|bar|baz)\\.o", "(foo|bar|baz)\\.o", 0 },
    
    // hiding leading metas
    { "[ ]foo", "[ ]foo", 0 },
    { "[#]foo", "[#]foo", 0 },
    { "[!]foo", "[!]foo", 0 },

    // directory slashes
    { "/foo", "(?:/|^)foo", 0 },
    { "[/]foo", "[/]foo", 0 }, 
    { "foo/", "foo(?:/|$)", 0 },
    { "foo[/]", "foo[/]", 0 },
    { "/foo/", "(?:/|^)foo(?:/|$)", 0 },

    // negation
    { "!abc", 0, "abc" },
    { "   !abc   ", 0, "abc" },
    { "!   abc   ", 0, "   abc" },

    // # is not a comment character after !
    { "!#abc", 0, "#abc" },
    { "![#]abc", 0, "[#]abc" },

    // / is magic after !
    { "!/foo", 0, "(?:/|^)foo" },
    { "![/]foo", 0, "[/]foo" },

    { 0, 0, 0 }
  };

  map<string, pcre::regex> in, out;
  for (size_t i = 0; tcases[i].str; i++)
    {
      UNIT_TEST_CHECKPOINT((FL("#%d: '%s'") % i % tcases[i].str).str().c_str());
      in.clear();
      out.clear();
      PARSE_ONE_LINENO(tcases[i].str, in, out, i);

      if (tcases[i].exp_in)
        {
          UNIT_TEST_CHECK(out.size() == 0);
          UNIT_TEST_CHECK(in.size() == 1);
          UNIT_TEST_CHECK_MSG(in.begin()->first == tcases[i].exp_in,
                              FL("in[0]=='%s' == '%s'") % in.begin()->first
                              % tcases[i].exp_in);
        }
      else if (tcases[i].exp_out)
        {
          UNIT_TEST_CHECK(in.size() == 0);
          UNIT_TEST_CHECK(out.size() == 1);
          UNIT_TEST_CHECK_MSG(out.begin()->first == tcases[i].exp_out,
                              FL("out[0]=='%s' == '%s'") % out.begin()->first
                              % tcases[i].exp_out);
        }
      else
        {
          UNIT_TEST_CHECK(in.size() == 0);
          UNIT_TEST_CHECK(out.size() == 0);
        }
    }
}

UNIT_TEST(ignore, line_interactions)
{
  map<string, pcre::regex> in, out;

  UNIT_TEST_CHECKPOINT("two included");
  PARSE_ONE("foo", in, out);
  PARSE_ONE("bar", in, out);
  UNIT_TEST_CHECK(in.size() == 2);
  UNIT_TEST_CHECK(out.size() == 0);

  in.clear();
  out.clear();
  UNIT_TEST_CHECKPOINT("two excluded");
  PARSE_ONE("!foo", in, out);
  PARSE_ONE("!bar", in, out);
  UNIT_TEST_CHECK(in.size() == 0);
  UNIT_TEST_CHECK(out.size() == 2);

  in.clear();
  out.clear();
  UNIT_TEST_CHECKPOINT("duplicate included");
  PARSE_ONE("foo", in, out);
  PARSE_ONE("foo", in, out);
  UNIT_TEST_CHECK(in.size() == 1);
  UNIT_TEST_CHECK(out.size() == 0);

  in.clear();
  out.clear();
  UNIT_TEST_CHECKPOINT("duplicate excluded");
  PARSE_ONE("!foo", in, out);
  PARSE_ONE("!foo", in, out);
  UNIT_TEST_CHECK(in.size() == 0);
  UNIT_TEST_CHECK(out.size() == 1);

  in.clear();
  out.clear();
  UNIT_TEST_CHECKPOINT("remove an include");
  PARSE_ONE("foo", in, out);
  PARSE_ONE("bar", in, out);
  PARSE_ONE("!foo", in, out);
  PARSE_ONE("!quux", in, out);
  UNIT_TEST_CHECK(in.size() == 1);
  UNIT_TEST_CHECK(out.size() == 1);
  UNIT_TEST_CHECK(in.begin()->first == string("bar"));
  UNIT_TEST_CHECK(out.begin()->first == string("quux"));
}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
