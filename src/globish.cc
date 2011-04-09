// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//               2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "sanity.hh"
#include "globish.hh"
#include "option.hh" // for arg_type
#include "numeric_vocab.hh"

#include <iterator>
#include <ostream>

using std::string;
using std::vector;
using std::back_inserter;
using std::back_insert_iterator;

// The algorithm here is originally from pdksh 5.  That implementation uses
// the high bit of unsigned chars as a quotation flag.  We can't do that,
// because we need to be utf8 clean.  Instead, we copy the string and
// replace "live" metacharacters with single bytes from the
// control-character range.  This is why bytes <= 0x1f are not allowed in the
// pattern.

enum metachar
{
  META_STAR = 1,   // *
  META_QUES,       // ?
  META_CC_BRA,     // [
  META_CC_INV_BRA, // [^ or [!
  META_CC_KET,     // ] (matches either of the above two)
  META_ALT_BRA,    // {
  META_ALT_OR,     // , (when found inside unquoted { ... })
  META_ALT_KET,    // }
};

// Compile a character class.

static string::const_iterator
compile_charclass(string const & pat, string::const_iterator p,
                  back_insert_iterator<string> & to,
                  origin::type made_from)
{
  string in_class;
  char bra = (char)META_CC_BRA;

  p++;
  E(p != pat.end(), made_from,
    F("invalid pattern '%s': unmatched '['") % pat);

  if (*p == '!' || *p == '^')
    {
      bra = (char)META_CC_INV_BRA;
      p++;
      E(p != pat.end(), made_from,
        F("invalid pattern '%s': unmatched '['") % pat);
    }

  while (p != pat.end() && *p != ']')
    {
      if (*p == '\\')
        {
          p++;
          if (p == pat.end())
            break;
        }
      // A dash at the beginning or end of the pattern is literal.
      else if (*p == '-'
               && !in_class.empty()
               && p + 1 != pat.end()
               && p[1] != ']')
        {
          p++;
          if (*p == '\\')
            p++;
          if (p == pat.end())
            break;

          // the cast is needed because boost::format will not obey the %x
          // if given a 'char'.
          E((widen<unsigned int, char>(*p)) >= ' ', made_from,
            F("invalid pattern '%s': control character 0x%02x is not allowed")
            % pat % (widen<unsigned int, char>(*p)));

          unsigned int start = widen<unsigned int, char>(in_class.end()[-1]);
          unsigned int stop = widen<unsigned int, char>(*p);

          E(start != stop, made_from,
            F("invalid pattern '%s': "
              "one-element character ranges are not allowed") % pat);
          E(start < stop, made_from,
            F("invalid pattern '%s': "
              "endpoints of a character range must be in "
              "ascending numeric order") % pat);
          E(start < 0x80 && stop < 0x80, made_from,
            F("invalid pattern '%s': cannot use non-ASCII characters "
              "in classes") % pat);

          L(FL("expanding range from %X (%c) to %X (%c)")
            % (start + 1) % (char)(start + 1) % stop % (char)stop);

          for (unsigned int r = start + 1; r < stop; r++)
            in_class.push_back((char)r);
        }
      else
        E(*p != '[', made_from,
          F("syntax error in '%s': "
            "character classes may not be nested") % pat);

      E((widen<unsigned int, char>(*p)) >= ' ', made_from,
        F("invalid pattern '%s': control character 0x%02x is not allowed")
        % pat % (widen<unsigned int, char>(*p)));

      E((widen<unsigned int, char>(*p)) < 0x80, made_from,
        F("invalid pattern '%s': cannot use non-ASCII characters in classes")
        % pat);

      in_class.push_back(*p);
      p++;
    }

  E(p != pat.end(), made_from,
    F("invalid pattern '%s': unmatched '['") % pat);

  E(!in_class.empty(), made_from,
    F("invalid pattern '%s': empty character class") % pat);

  // minor optimization: one-element non-inverted character class becomes
  // the character.
  if (bra == (char)META_CC_BRA && in_class.size() == 1)
    *to++ = in_class[0];
  else
    {
      *to++ = bra;
      std::sort(in_class.begin(), in_class.end());
      std::copy(in_class.begin(), in_class.end(), to);
      *to++ = (char)META_CC_KET;
    }
  return p;
}

// Compile one fragment of a glob pattern.

static void
compile_frag(string const & pat, back_insert_iterator<string> & to,
             origin::type made_from)
{
  unsigned int brace_depth = 0;

  for (string::const_iterator p = pat.begin(); p != pat.end(); p++)
    switch (*p)
      {
      default:
        E((widen<unsigned int, char>(*p)) >= ' ', made_from,
          F("invalid pattern '%s': control character 0x%02x is not allowed")
          % pat % (widen<unsigned int, char>(*p)));

        *to++ = *p;
        break;

      case '*':
        // optimization: * followed by any sequence of ?s and *s is
        // equivalent to the number of ?s that appeared in the sequence,
        // followed by a single star.  the latter can be matched without
        // nearly as much backtracking.

        for (p++; p != pat.end(); p++)
          {
            if (*p == '?')
              *to++ = META_QUES;
            else if (*p != '*')
              break;
          }

        p--;
        *to++ = META_STAR;
        break;

      case '?':
        *to++ = META_QUES;
        break;

      case '\\':
        p++;
        E(p != pat.end(), made_from,
          F("invalid pattern '%s': un-escaped \\ at end") % pat);

        E((widen<unsigned int, char>(*p)) >= ' ', made_from,
          F("invalid pattern '%s': control character 0x%02x is not allowed")
          % pat % (widen<unsigned int, char>(*p)));

        *to++ = *p;
        break;

      case '[':
        p = compile_charclass(pat, p, to, made_from);
        break;

      case ']':
        E(false, made_from, F("invalid pattern '%s': unmatched ']'") % pat);

      case '{':
        // There's quite a bit of optimization we could be doing on
        // alternatives, but it's hairy, especially if you get into
        // nested alternatives; so we're not doing any of it now.
        // (Look at emacs's regexp-opt.el for inspiration.)
        brace_depth++;
        E(brace_depth < 6, made_from,
          F("invalid pattern '%s': braces nested too deeply") % pat);
        *to++ = META_ALT_BRA;
        break;

      case ',':
        if (brace_depth > 0)
          *to++ = META_ALT_OR;
        else
          *to++ = ',';
        break;

      case '}':
        E(brace_depth > 0, made_from,
          F("invalid pattern '%s': unmatched '}'") % pat);
        brace_depth--;
        *to++ = META_ALT_KET;
        break;
      }

  E(brace_depth == 0, made_from,
    F("invalid pattern '%s': unmatched '{'") % pat);
}

// common code used by the constructors.

static inline string
compile(string const & pat, origin::type made_from)
{
  string s;
  back_insert_iterator<string> to = back_inserter(s);
  compile_frag(pat, to, made_from);
  return s;
}

static inline string
compile(vector<arg_type>::const_iterator const & beg,
        vector<arg_type>::const_iterator const & end)
{
  if (end - beg == 0)
    return "";
  if (end - beg == 1)
    return compile((*beg)(), origin::user);

  string s;
  back_insert_iterator<string> to = back_inserter(s);

  *to++ = META_ALT_BRA;
  vector<arg_type>::const_iterator i = beg;
  for (;;)
    {
      compile_frag((*i)(), to, origin::user);
      i++;
      if (i == end)
        break;
      *to++ = META_ALT_OR;
    }
  *to++ = META_ALT_KET;
  return s;
}

globish::globish(string const & p, origin::type made_from)
  : origin_aware(made_from),
    compiled_pattern(compile(p, made_from)) {}
globish::globish(char const * p, origin::type made_from)
  : origin_aware(made_from),
    compiled_pattern(compile(p, made_from)) {}

globish::globish(vector<arg_type> const & p)
  : origin_aware(origin::user),
    compiled_pattern(compile(p.begin(), p.end())) {}
globish::globish(vector<arg_type>::const_iterator const & beg,
                 vector<arg_type>::const_iterator const & end)
  : origin_aware(origin::user),
    compiled_pattern(compile(beg, end)) {}

// Debugging.

static string
decode(string::const_iterator p, string::const_iterator end, bool escaped = true)
{
  string s;
  for (; p != end; p++)
    switch (*p)
      {
      case META_STAR:       s.push_back('*'); break;
      case META_QUES:       s.push_back('?'); break;
      case META_CC_BRA:     s.push_back('['); break;
      case META_CC_KET:     s.push_back(']'); break;
      case META_CC_INV_BRA: s.push_back('[');
        s.push_back('!'); break;

      case META_ALT_BRA:    s.push_back('{'); break;
      case META_ALT_KET:    s.push_back('}'); break;
      case META_ALT_OR:     s.push_back(','); break;

        // Some of these are only special in certain contexts,
        // so for these contexts we don't want to escape them
      case '[': case ']': case '-': case '!': case '^':
      case '{': case '}': case ',':
      case '*': case '?': case '\\':
        if (escaped)
          s.push_back('\\');
        // fall through
      default:
        s.push_back(*p);
      }
  return s;
}

string
globish::operator()() const
{
  return decode(compiled_pattern.begin(), compiled_pattern.end());
}

string
globish::unescaped() const
{
  return decode(compiled_pattern.begin(), compiled_pattern.end(), false);
}

bool
globish::contains_meta_chars() const
{
  string::const_iterator p = compiled_pattern.begin();
  for (; p != compiled_pattern.end(); p++)
    switch (*p)
      {
      case META_STAR:
      case META_QUES:
      case META_CC_BRA:
      case META_CC_KET:
      case META_CC_INV_BRA:
      case META_ALT_BRA:
      case META_ALT_KET:
      case META_ALT_OR:
        return true;
      }
  return false;
}

template <> void dump(globish const & g, string & s)
{
  s = g();
}

std::ostream & operator<<(std::ostream & o, globish const & g)
{
  return o << g();
}

// Matching.

static string::const_iterator
find_next_subpattern(string::const_iterator p,
                     string::const_iterator pe,
                     bool want_alternatives)
{
  L(FL("Finding subpattern in '%s'") % decode(p, pe));
  unsigned int depth = 1;
  for (; p != pe; p++)
    switch (*p)
      {
      default:
        break;

      case META_ALT_BRA:
        depth++;
        break;

      case META_ALT_KET:
        depth--;
        if (depth == 0)
          return p + 1;
        break;

      case META_ALT_OR:
        if (depth == 1 && want_alternatives)
          return p + 1;
        break;
      }

  I(false);
}


static bool
do_match(string::const_iterator sb, string::const_iterator se,
         string::const_iterator p, string::const_iterator pe)
{
  unsigned int sc, pc;
  string::const_iterator s(sb);

  L(FL("subpattern: '%s' against '%s'") % string(s, se) % decode(p, pe));

  while (p < pe)
    {
      // pc will be the current pattern character
      // p will point after pc
      pc = widen<unsigned int, char>(*p++);
      // sc will be the current string character
      // s will point to sc
      if(s < se)
        {
          sc = widen<unsigned int, char>(*s);
        }
      else
        {
          sc = 0;
        }
      switch (pc)
        {
        default:           // literal
          if (sc != pc)
            return false;
          break;

        case META_QUES: // any single character
          if (sc == 0)
            return false;
          break;

        case META_CC_BRA:  // any of these characters
        {
          bool matched = false;
          I(p < pe);
          I(*p != META_CC_KET);
          do
            {
              if (widen<unsigned int, char>(*p) == sc)
                matched = true;
              p++;
              I(p < pe);
            }
          while (*p != META_CC_KET);
          if (!matched)
            return false;
        }
        p++;
        break;

        case META_CC_INV_BRA:  // any but these characters
          I(p < pe);
          I(*p != META_CC_KET);
          do
            {
              if (widen<unsigned int, char>(*p) == sc)
                return false;
              p++;
              I(p < pe);
            }
          while (*p != META_CC_KET);
          p++;
          break;

        case META_STAR:    // zero or more arbitrary characters
          if (p == pe)
            return true; // star at end always matches, if we get that far

          pc = widen<unsigned int, char>(*p);
          // If the next character in p is not magic, we can only match
          // starting from places in s where that character appears.
          if (pc >= ' ')
            {
              L(FL("after *: looking for '%c' in '%s'")
                % (char)pc % string(s, se));
              p++;
              for (;;)
                {
                  ++s;
                  if (sc == pc && do_match(s, se, p, pe))
                    return true;
                  if (s >= se)
                    break;
                  sc = widen<unsigned int, char>(*s);
                }
            }
          else
            {
              L(FL("metacharacter after *: doing it the slow way"));
              do
                {
                  if (do_match(s, se, p, pe))
                    return true;
                  s++;
                }
              while (s < se);
            }
          return false;

        case META_ALT_BRA:
        {
          string::const_iterator prest, psub, pnext;
          string::const_iterator srest;

          prest = find_next_subpattern(p, pe, false);
          psub = p;
          // [ psub ... prest ) is the current bracket pair
          // (including the *closing* braket, but not the opening braket)
          do
            {
              pnext = find_next_subpattern(psub, pe, true);
              // pnext points just after a comma or the closing braket
              // [ psub ... pnext ) is one branch with trailing delimiter
              srest = (prest == pe ? se : s);
              for (; srest < se; srest++)
                {
                  if (do_match(s, srest, psub, pnext - 1)
                      && do_match(srest, se, prest, pe))
                    return true;
                }
              // try the empty target too
              if (do_match(s, srest, psub, pnext - 1)
                  && do_match(srest, se, prest, pe))
                return true;

              psub = pnext;
            }
          while (pnext < prest);
          return false;
        }
        }
      if (s < se)
        {
          ++s;
        }
    }
  return s == se;
}

bool globish::matches(string const & target) const
{
  bool result;

  // The empty pattern matches nothing.
  if (compiled_pattern.empty())
    result = false;
  else
    result = do_match (target.begin(), target.end(),
                       compiled_pattern.begin(), compiled_pattern.end());

  L(FL("matching '%s' against '%s': %s")
    % target % (*this)() % (result ? "matches" : "does not match"));
  return result;
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
