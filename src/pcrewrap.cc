// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "pcrewrap.hh"
#include "sanity.hh"
#include <cstring>
#include <map>
#include <vector>

// This dirty trick is necessary to prevent the 'pcre' typedef defined by
// pcre.h from colliding with namespace pcre.
#define pcre pcre_t
#include "pcre.h"
#undef pcre

using std::make_pair;
using std::map;
using std::pair;
using std::string;
using std::vector;

static NORETURN(void pcre_compile_error(int errcode, char const * err,
                                        int erroff, char const * pattern,
                                        origin::type caused_by));
static NORETURN(void pcre_study_error(char const * err, char const * pattern,
                                      origin::type caused_by));
static NORETURN(void pcre_exec_error(int errcode,
                                     origin::type regex_from,
                                     origin::type subject_from));

inline unsigned int
flags_to_internal(pcre::flags f)
{
  using namespace pcre;
#define C(f_, x) (((f_) & (x)) ? PCRE_##x : 0)
  unsigned int i = 0;
  i |= C(f, NEWLINE_CR);
  i |= C(f, NEWLINE_LF);
  // NEWLINE_CRLF == NEWLINE_CR|NEWLINE_LF and so is handled above
  i |= C(f, ANCHORED);
  i |= C(f, NOTBOL);
  i |= C(f, NOTEOL);
  i |= C(f, NOTEMPTY);
  i |= C(f, CASELESS);
  i |= C(f, DOLLAR_ENDONLY);
  i |= C(f, DOTALL);
  i |= C(f, DUPNAMES);
  i |= C(f, EXTENDED);
  i |= C(f, FIRSTLINE);
  i |= C(f, MULTILINE);
  i |= C(f, UNGREEDY);
#undef C
  return i;
}

inline unsigned int
get_capturecount(void const * bd)
{
  unsigned int cc;
  int err = pcre_fullinfo(static_cast<pcre_t const *>(bd), 0,
                          PCRE_INFO_CAPTURECOUNT,
                          static_cast<void *>(&cc));
  I(err == 0);
  return cc;
}

namespace pcre
{
  typedef map<char const *,
              pair<struct real_pcre const *, struct pcre_extra const *> >
              regex_cache;

  class regex_cache_manager
  {
public:
    regex_cache::const_iterator find(char const * pattern)
      {
        return cache.find(pattern);
      }

    void store(char const * pattern,
               pair<struct real_pcre const *, struct pcre_extra const *>
               data)
      {
        cache[pattern] = data;
      }

    regex_cache::const_iterator end()
      {
        return cache.end();
      }

    ~regex_cache_manager()
      {
        for (regex_cache::iterator iter = cache.begin();
             iter != cache.end();
             ++iter)
          {
            if (iter->second.first)
              pcre_free(const_cast<pcre_t *>(iter->second.first));

            if (iter->second.second)
              pcre_free(const_cast<pcre_extra *>(iter->second.second));
          }
      }
private:
    regex_cache cache;
  };

  regex_cache_manager compiled;

  void regex::init(char const * pattern, flags options)
  {
    int errcode;
    int erroff;
    char const * err;
    // use the cached data if we have it
    regex_cache::const_iterator iter = compiled.find(pattern);
    if (iter != compiled.end())
      {
        basedat = iter->second.first;
        extradat = iter->second.second;
        return;
      }
    // not in cache - compile them then store in cache
    basedat = pcre_compile2(pattern, flags_to_internal(options),
                            &errcode, &err, &erroff, 0);
    if (!basedat)
      pcre_compile_error(errcode, err, erroff, pattern, made_from);

    pcre_extra *ed = pcre_study(basedat, 0, &err);
    if (err)
      pcre_study_error(err, pattern, made_from);
    if (!ed)
      {
        // I resent that C++ requires this cast.
        ed = (pcre_extra *)pcre_malloc(sizeof(pcre_extra));
        std::memset(ed, 0, sizeof(pcre_extra));
      }

    // We set a fairly low recursion depth to avoid stack overflow.
    // Per pcrestack(3), one should assume 500 bytes per recursion;
    // it should be safe to let pcre have a megabyte of stack, so
    // that's a depth of 2000, give or take.  (For reference, the
    // default stack limit on Linux is 8MB.)
    ed->flags |= PCRE_EXTRA_MATCH_LIMIT_RECURSION;
    ed->match_limit_recursion = 2000;
    extradat = ed;
    // store in cache
    compiled.store(pattern, make_pair(basedat, extradat));
  }

  regex::regex(char const * pattern, origin::type whence, flags options)
    : made_from(whence)
  {
    this->init(pattern, options);
  }

  regex::regex(string const & pattern, origin::type whence, flags options)
    : made_from(whence)
  {
    this->init(pattern.c_str(), options);
  }

  regex::~regex()
  {
  }

  bool
  regex::match(string const & subject, origin::type subject_origin,
               flags options) const
  {
    int rc = pcre_exec(basedat, extradat,
                       subject.data(), subject.size(),
                       0, flags_to_internal(options), 0, 0);
    if (rc == 0)
      return true;
    else if (rc == PCRE_ERROR_NOMATCH)
      return false;
    else
      pcre_exec_error(rc, made_from, subject_origin);
  }

  bool
  regex::match(string const & subject, origin::type subject_origin,
               vector<string> & matches, flags options) const
  {
    matches.clear();

    // retrieve the capture count of the pattern from pcre_fullinfo,
    // because pcre_exec might not signal trailing unmatched subpatterns
    // i.e. if "abc" matches "(abc)(de)?", the match count is two, not
    // the expected three
    int cap_count = 0;
    int rc = pcre_fullinfo(basedat, extradat, PCRE_INFO_CAPTURECOUNT, &cap_count);
    I(rc == 0);

    // the complete regex is captured as well
    cap_count += 1;

    int worksize = cap_count * 3;

    // "int ovector[worksize]" is C99 only (not valid C++, but allowed by gcc/clang)
    // boost::shared_array is I think not plannned to be part of C++0x
    class xyzzy {
      int *data;
    public:
      xyzzy(int len) : data(new int[len]) {}
      ~xyzzy() { delete[] data; }
      operator int*() { return data; }
    } ovector(worksize);

    rc = pcre_exec(basedat, extradat,
                   subject.data(), subject.size(),
                   0, flags_to_internal(options), ovector, worksize);

    // since we dynamically set the work size, we should
    // always get either a negative (error) or >= 1 match count
    I(rc != 0);

    if (rc == PCRE_ERROR_NOMATCH)
      return false;
    else if (rc < 0)
      pcre_exec_error(rc, made_from, subject_origin); // throws

    for (int i=0; i < cap_count; ++i)
      {
        string match;
        // not an empty match
        if (ovector[2*i] != -1 && ovector[2*i+1] != -1)
          match.assign(subject, ovector[2*i], ovector[2*i+1] - ovector[2*i]);
        matches.push_back(match);
      }

    return true;
  }
} // namespace pcre

// When the library returns an error, these functions discriminate between
// bugs in monotone and user errors in regexp writing.
static void
pcre_compile_error(int errcode, char const * err,
                   int erroff, char const * pattern,
                   origin::type caused_by)
{
  // One of the more entertaining things about the PCRE API is that
  // while the numeric error codes are documented, they do not get
  // symbolic names.

  switch (errcode)
    {
    case 21: // failed to get memory
      throw std::bad_alloc();

    case 10: // [code allegedly not in use]
    case 11: // internal error: unexpected repeat
    case 16: // erroffset passed as NULL
    case 17: // unknown option bit(s) set
    case 19: // [code allegedly not in use]
    case 23: // internal error: code overflow
    case 33: // [code allegedly not in use]
    case 50: // [code allegedly not in use]
    case 52: // internal error: overran compiling workspace
    case 53: // internal error: previously-checked referenced subpattern
             // not found
      throw oops((F("while compiling regex '%s': %s") % pattern % err)
                 .str().c_str());

    default:
      // PCRE fails to distinguish between errors at no position and errors at
      // character offset 0 in the pattern, so in practice we give the
      // position-ful variant for all errors, but I'm leaving the == -1 check
      // here in case PCRE gets fixed.
      E(false, caused_by, (erroff == -1
                           ? (F("error in regex '%s': %s")
                              % pattern % err)
                           : (F("error near char %d of regex '%s': %s")
                              % (erroff + 1) % pattern % err)
                           ));
    }
}

static void
pcre_study_error(char const * err, char const * pattern,
                 origin::type caused_by)
{
  // This interface doesn't even *have* error codes.
  // If the error is not out-of-memory, it's a bug.
  if (!std::strcmp(err, "failed to get memory"))
    throw std::bad_alloc();
  else
    throw oops((F("while studying regex '%s': %s") % pattern % err)
               .str().c_str());
}

static void
pcre_exec_error(int errcode, origin::type regex_from, origin::type subject_from)
{
  // This interface provides error codes with symbolic constants for them!
  // But it doesn't provide string versions of them.  As most of them
  // indicate bugs in monotone, it's not worth defining our own strings.

  switch(errcode)
    {
    case PCRE_ERROR_NOMEMORY:
      throw std::bad_alloc();

    case PCRE_ERROR_MATCHLIMIT:
      E(false, subject_from,
        F("backtrack limit exceeded in regular expression matching"));

    case PCRE_ERROR_RECURSIONLIMIT:
      E(false, subject_from,
        F("recursion limit exceeded in regular expression matching"));

    case PCRE_ERROR_BADUTF8:
    case PCRE_ERROR_BADUTF8_OFFSET:
      E(false, subject_from,
        F("invalid UTF-8 sequence found during regular expression matching"));

    default:
      throw oops((F("pcre_exec returned %d") % errcode)
                 .str().c_str());
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
