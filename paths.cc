// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <sstream>

#include "paths.hh"
#include "file_io.hh"
#include "charset.hh"
#include "safe_map.hh"

using std::exception;
using std::ostream;
using std::ostringstream;
using std::string;
using std::vector;
using std::map;
using std::make_pair;

// some structure to ensure we aren't doing anything broken when resolving
// filenames.  the idea is to make sure
//   -- we don't depend on the existence of something before it has been set
//   -- we don't re-set something that has already been used
//   -- sometimes, we use the _non_-existence of something, so we shouldn't
//      set anything whose un-setted-ness has already been used
template <typename T>
struct access_tracker
{
  void set(T const & val, bool may_be_initialized)
  {
    I(may_be_initialized || !initialized);
    I(!very_uninitialized);
    I(!used);
    initialized = true;
    value = val;
  }
  T const & get()
  {
    I(initialized);
    used = true;
    return value;
  }
  T const & get_but_unused()
  {
    I(initialized);
    return value;
  }
  void may_not_initialize()
  {
    I(!initialized);
    very_uninitialized = true;
  }
  // for unit tests
  void unset()
  {
    used = initialized = very_uninitialized = false;
  }
  T value;
  bool initialized, used, very_uninitialized;
  access_tracker() : initialized(false), used(false), very_uninitialized(false) {};
};

// paths to use in interpreting paths from various sources,
// conceptually:
//    working_root / initial_rel_path == initial_abs_path

// initial_abs_path is for interpreting relative system_path's
static access_tracker<system_path> initial_abs_path;
// initial_rel_path is for interpreting external file_path's
// we used to make it a file_path, but then you can't run monotone from
// inside the _MTN/ dir (even when referring to files outside the _MTN/
// dir).  use of a bare string requires some caution but does work.
static access_tracker<string> initial_rel_path;
// working_root is for converting file_path's and bookkeeping_path's to
// system_path's.
static access_tracker<system_path> working_root;

void
save_initial_path()
{
  // FIXME: BUG: this only works if the current working dir is in utf8
  initial_abs_path.set(system_path(get_current_working_dir(),
                                   origin::system), false);
  L(FL("initial abs path is: %s") % initial_abs_path.get_but_unused());
}

///////////////////////////////////////////////////////////////////////////
// verifying that internal paths are indeed normalized.
// this code must be superfast
///////////////////////////////////////////////////////////////////////////

// normalized means:
//  -- / as path separator
//  -- not an absolute path (on either posix or win32)
//     operationally, this means: first character != '/', first character != '\',
//     second character != ':'
//  -- no illegal characters
//     -- 0x00 -- 0x1f, 0x7f, \ are the illegal characters.  \ is illegal
//        unconditionally to prevent people checking in files on posix that
//        have a different interpretation on win32
//     -- (may want to allow 0x0a and 0x0d (LF and CR) in the future, but this
//        is blocked on manifest format changing)
//        (also requires changes to 'automate inventory', possibly others, to
//        handle quoting)
//  -- no doubled /'s
//  -- no trailing /
//  -- no "." or ".." path components

static inline bool
bad_component(string const & component)
{
  if (component.empty())
    return true;
  if (component == ".")
    return true;
  if (component == "..")
    return true;
  return false;
}

static inline bool
has_bad_chars(string const & path)
{
  for (string::const_iterator c = path.begin(); LIKELY(c != path.end()); c++)
    {
      // char is often a signed type; convert to unsigned to ensure that
      // bytes 0x80-0xff are considered > 0x1f.
      u8 x = (u8)*c;
      // 0x5c is '\\'; we use the hex constant to make the dependency on
      // ASCII encoding explicit.
      if (UNLIKELY(x <= 0x1f || x == 0x5c || x == 0x7f))
        return true;
    }
  return false;
}

// as above, but disallows / as well.
static inline bool
has_bad_component_chars(string const & pc)
{
  for (string::const_iterator c = pc.begin(); LIKELY(c != pc.end()); c++)
    {
      // char is often a signed type; convert to unsigned to ensure that
      // bytes 0x80-0xff are considered > 0x1f.
      u8 x = (u8)*c;
      // 0x2f is '/' and 0x5c is '\\'; we use hex constants to make the
      // dependency on ASCII encoding explicit.
      if (UNLIKELY(x <= 0x1f || x == 0x2f || x == 0x5c || x == 0x7f))
        return true;
    }
  return false;

}

static bool
is_absolute_here(string const & path)
{
  if (path.empty())
    return false;
  if (path[0] == '/')
    return true;
#ifdef WIN32
  if (path[0] == '\\')
    return true;
  if (path.size() > 1 && path[1] == ':')
    return true;
#endif
  return false;
}

static inline bool
is_absolute_somewhere(string const & path)
{
  if (path.empty())
    return false;
  if (path[0] == '/')
    return true;
  if (path[0] == '\\')
    return true;
  if (path.size() > 1 && path[1] == ':')
    return true;
  return false;
}

// fully_normalized_path verifies a complete pathname for validity and
// having been properly normalized (as if by normalize_path, below).
static inline bool
fully_normalized_path(string const & path)
{
  // empty path is fine
  if (path.empty())
    return true;
  // could use is_absolute_somewhere, but this is the only part of it that
  // wouldn't be redundant
  if (path.size() > 1 && path[1] == ':')
    return false;
  // first scan for completely illegal bytes
  if (has_bad_chars(path))
    return false;
  // now check each component
  string::size_type start = 0, stop;
  while (1)
    {
      stop = path.find('/', start);
      if (stop == string::npos)
        break;
      string const & s(path.substr(start, stop - start));
      if (bad_component(s))
        return false;
      start = stop + 1;
    }

  string const & s(path.substr(start));
  return !bad_component(s);
}

// This function considers _MTN, _MTn, _MtN, _mtn etc. to all be bookkeeping
// paths, because on case insensitive filesystems, files put in any of them
// may end up in _MTN instead.  This allows arbitrary code execution.  A
// better solution would be to fix this in the working directory writing
// code -- this prevents all-unix projects from naming things "_mtn", which
// is less rude than when the bookkeeping root was "MT", but still rude --
// but as a temporary security kluge it works.
static inline bool
in_bookkeeping_dir(string const & path)
{
  if (path.empty() || (path[0] != '_'))
    return false;
  if (path.size() == 1 || (path[1] != 'M' && path[1] != 'm'))
    return false;
  if (path.size() == 2 || (path[2] != 'T' && path[2] != 't'))
    return false;
  if (path.size() == 3 || (path[3] != 'N' && path[3] != 'n'))
    return false;
  // if we've gotten here, the first three letters are _, M, T, and N, in
  // either upper or lower case.  So if that is the whole path, or else if it
  // continues but the next character is /, then this is a bookkeeping path.
  if (path.size() == 4 || (path[4] == '/'))
    return true;
  return false;
}

static inline bool
is_valid_internal(string const & path)
{
  return (fully_normalized_path(path)
          && !in_bookkeeping_dir(path));
}

static string
normalize_path(string const & in)
{
  string inT = in;
  string leader;
  MM(inT);

#ifdef WIN32
  // the first thing we do is kill all the backslashes
  for (string::iterator i = inT.begin(); i != inT.end(); i++)
    if (*i == '\\')
      *i = '/';
#endif

  if (is_absolute_here (inT))
    {
      if (inT[0] == '/')
        {
          leader = "/";
          inT = inT.substr(1);

          if (!inT.empty() && inT[0] == '/')
            {
              // if there are exactly two slashes at the beginning they
              // are both preserved.  three or more are the same as one.
              string::size_type f = inT.find_first_not_of("/");
              if (f == string::npos)
                f = inT.size();
              if (f == 1)
                leader = "//";
              inT = inT.substr(f);
            }
        }
#ifdef WIN32
      else
        {
          I(inT.size() > 1 && inT[1] == ':');
          if (inT.size() > 2 && inT[2] == '/')
            {
              leader = inT.substr(0, 3);
              inT = inT.substr(3);
            }
          else
            {
              leader = inT.substr(0, 2);
              inT = inT.substr(2);
            }
        }
#endif

      I(!is_absolute_here(inT));
      if (inT.empty())
        return leader;
    }

  vector<string> stack;
  string::const_iterator head, tail;
  string::size_type size_estimate = leader.size();
  for (head = inT.begin(); head != inT.end(); head = tail)
    {
      tail = head;
      while (tail != inT.end() && *tail != '/')
        tail++;

      string elt(head, tail);
      while (tail != inT.end() && *tail == '/')
        tail++;

      if (elt == ".")
        continue;
      // remove foo/.. element pairs; leave leading .. components alone
      if (elt == ".." && !stack.empty() && stack.back() != "..")
        {
          stack.pop_back();
          continue;
        }

      size_estimate += elt.size() + 1;
      stack.push_back(elt);
    }

  leader.reserve(size_estimate);
  for (vector<string>::const_iterator i = stack.begin(); i != stack.end(); i++)
    {
      if (i != stack.begin())
        leader += "/";
      leader += *i;
    }
  return leader;
}

static void
normalize_external_path(string const & path, string & normalized, bool to_workspace_root)
{
  if (!initial_rel_path.initialized)
    {
      // we are not in a workspace; treat this as an internal
      // path, and set the access_tracker() into a very uninitialised
      // state so that we will hit an exception if we do eventually
      // enter a workspace
      initial_rel_path.may_not_initialize();
      normalized = path;
      E(is_valid_internal(path), origin::user,
        F("path '%s' is invalid") % path);
    }
  else
    {
      E(!is_absolute_here(path), origin::user,
        F("absolute path '%s' is invalid") % path);
      string base;
      try
        {
          if (to_workspace_root)
            base = "";
          else
            base = initial_rel_path.get();

          if (base == "")
            normalized = normalize_path(path);
          else
            normalized = normalize_path(base + "/" + path);
        }
      catch (exception &)
        {
          E(false, origin::user, F("path '%s' is invalid") % path);
        }
      if (normalized == ".")
        normalized = string("");
      E(fully_normalized_path(normalized), origin::user,
        F("path '%s' is invalid") % normalized);
    }
}

///////////////////////////////////////////////////////////////////////////
// single path component handling.
///////////////////////////////////////////////////////////////////////////

// these constructors confirm that what they are passed is a legitimate
// component.  note that the empty string is a legitimate component,
// but is not acceptable to bad_component (above) and therefore we have
// to open-code most of those checks.
path_component::path_component(utf8 const & d)
  : origin_aware(d.made_from), data(d())
{
  MM(data);
  I(!has_bad_component_chars(data) && data != "." && data != "..");
}

path_component::path_component(string const & d, origin::type whence)
  : origin_aware(whence), data(d)
{
  MM(data);
  I(utf8_validate(utf8(data, origin::internal))
    && !has_bad_component_chars(data)
    && data != "." && data != "..");
}

path_component::path_component(char const * d)
  : data(d)
{
  MM(data);
  I(utf8_validate(utf8(data, origin::internal))
    && !has_bad_component_chars(data)
    && data != "." && data != "..");
}

std::ostream & operator<<(std::ostream & s, path_component const & pc)
{
  return s << pc();
}

template <> void dump(path_component const & pc, std::string & to)
{
  to = pc();
}

///////////////////////////////////////////////////////////////////////////
// complete paths to files within a working directory
///////////////////////////////////////////////////////////////////////////

file_path::file_path(file_path::source_type type, string const & path, bool to_workspace_root)
{
  MM(path);
  I(utf8_validate(utf8(path, origin::internal)));
  if (type == external)
    {
      string normalized;
      normalize_external_path(path, normalized, to_workspace_root);
      E(!in_bookkeeping_dir(normalized), origin::user,
        F("path '%s' is in bookkeeping dir") % normalized);
      data = normalized;
    }
  else
    data = path;
  MM(data);
  I(is_valid_internal(data));
}

file_path::file_path(file_path::source_type type, utf8 const & path,
                     bool to_workspace_root)
  : any_path(path.made_from)
{
  MM(path);
  E(utf8_validate(path), made_from, F("Invalid utf8"));
  if (type == external)
    {
      string normalized;
      normalize_external_path(path(), normalized, to_workspace_root);
      E(!in_bookkeeping_dir(normalized), origin::user,
        F("path '%s' is in bookkeeping dir") % normalized);
      data = normalized;
    }
  else
    data = path();
  MM(data);
  I(is_valid_internal(data));
}

bookkeeping_path::bookkeeping_path(char const * const path)
{
  I(fully_normalized_path(path));
  I(in_bookkeeping_dir(path));
  data = path;
}

bookkeeping_path::bookkeeping_path(string const & path, origin::type made_from)
{
  E(fully_normalized_path(path), made_from, F("Path is not normalized"));
  E(in_bookkeeping_dir(path), made_from,
    F("Bookkeeping path is not in bookkeeping dir"));
  data = path;
}

bool
bookkeeping_path::external_string_is_bookkeeping_path(utf8 const & path)
{
  // FIXME: this charset casting everywhere is ridiculous
  string normalized;
  try
    {
      normalize_external_path(path(), normalized, false);
    }
  catch (recoverable_failure &)
    {
      return false;
    }
  return internal_string_is_bookkeeping_path(utf8(normalized, path.made_from));
}
bool bookkeeping_path::internal_string_is_bookkeeping_path(utf8 const & path)
{
  return in_bookkeeping_dir(path());
}

///////////////////////////////////////////////////////////////////////////
// splitting/joining
// this code must be superfast
// it depends very much on knowing that it can only be applied to fully
// normalized, relative, paths.
///////////////////////////////////////////////////////////////////////////

// this peels off the last component of any path and returns it.
// the last component of a path with no slashes in it is the complete path.
// the last component of a path referring to the root directory is an
// empty string.
path_component
any_path::basename() const
{
  string const & s = data;
  string::size_type sep = s.rfind('/');
#ifdef WIN32
  if (sep == string::npos && s.size()>= 2 && s[1] == ':')
    sep = 1;
#endif
  if (sep == string::npos)
    return path_component(s, 0);  // force use of short circuit
  if (sep == s.size())
    return path_component();
  return path_component(s, sep + 1);
}

// this returns all but the last component of any path.  It has to take
// care at the root.
any_path
any_path::dirname() const
{
  string const & s = data;
  string::size_type sep = s.rfind('/');
#ifdef WIN32
  if (sep == string::npos && s.size()>= 2 && s[1] == ':')
    sep = 1;
#endif
  if (sep == string::npos)
    return any_path();

  // dirname() of the root directory is itself
  if (sep == s.size() - 1)
    return *this;

  // dirname() of a direct child of the root is the root
  if (sep == 0 || (sep == 1 && s[1] == '/')
#ifdef WIN32
      || (sep == 1 || sep == 2 && s[1] == ':')
#endif
      )
    return any_path(s, 0, sep+1);

  return any_path(s, 0, sep);
}

// these variations exist to get the return type right.  also,
// file_path dirname() can be a little simpler.
file_path
file_path::dirname() const
{
  string const & s = data;
  string::size_type sep = s.rfind('/');
  if (sep == string::npos)
    return file_path();
  return file_path(s, 0, sep);
}

system_path
system_path::dirname() const
{
  string const & s = data;
  string::size_type sep = s.rfind('/');
#ifdef WIN32
  if (sep == string::npos && s.size()>= 2 && s[1] == ':')
    sep = 1;
#endif
  I(sep != string::npos);

  // dirname() of the root directory is itself
  if (sep == s.size() - 1)
    return *this;

  // dirname() of a direct child of the root is the root
  if (sep == 0 || (sep == 1 && s[1] == '/')
#ifdef WIN32
      || (sep == 1 || sep == 2 && s[1] == ':')
#endif
      )
    return system_path(s, 0, sep+1);

  return system_path(s, 0, sep);
}


// produce dirname and basename at the same time
void
file_path::dirname_basename(file_path & dir, path_component & base) const
{
  string const & s = data;
  string::size_type sep = s.rfind('/');
  if (sep == string::npos)
    {
      dir = file_path();
      base = path_component(s, 0);
    }
  else
    {
      I(sep < s.size() - 1); // last component must have at least one char
      dir = file_path(s, 0, sep);
      base = path_component(s, sep + 1);
    }
}

// returns true if this path is beneath other
bool
file_path::is_beneath_of(const file_path & other) const
{
  if (other.empty())
    return true;

  file_path basedir = dirname();
  while (!basedir.empty())
    {
      L(FL("base: %s, other: %s") % basedir % other);
      if (basedir == other)
        return true;
      basedir = basedir.dirname();
    }
  return false;
}

// count the number of /-separated components of the path.
unsigned int
file_path::depth() const
{
  if (data.empty())
    return 0;

  unsigned int components = 1;
  for (string::const_iterator p = data.begin(); p != data.end(); p++)
    if (*p == '/')
      components++;

  return components;
}

///////////////////////////////////////////////////////////////////////////
// localizing file names (externalizing them)
// this code must be superfast when there is no conversion needed
///////////////////////////////////////////////////////////////////////////

string
any_path::as_external() const
{
#ifdef __APPLE__
  // on OS X paths for the filesystem/kernel are UTF-8 encoded, regardless of
  // locale.
  return data;
#else
  // on normal systems we actually have some work to do, alas.
  // not much, though, because utf8_to_system_string does all the hard work.
  // it is carefully optimized.  do not screw it up.
  external out;
  utf8_to_system_strict(utf8(data, made_from), out);
  return out();
#endif
}

///////////////////////////////////////////////////////////////////////////
// writing out paths
///////////////////////////////////////////////////////////////////////////

ostream &
operator <<(ostream & o, any_path const & a)
{
  o << a.as_internal();
  return o;
}

template <>
void dump(file_path const & p, string & out)
{
  ostringstream oss;
  oss << p << '\n';
  out = oss.str();
}

template <>
void dump(system_path const & p, string & out)
{
  ostringstream oss;
  oss << p << '\n';
  out = oss.str();
}

template <>
void dump(bookkeeping_path const & p, string & out)
{
  ostringstream oss;
  oss << p << '\n';
  out = oss.str();
}

///////////////////////////////////////////////////////////////////////////
// path manipulation
// this code's speed does not matter much
///////////////////////////////////////////////////////////////////////////

// relies on its arguments already being validated, except that you may not
// append the empty path component, and if you are appending to the empty
// path, you may not create an absolute path or a path into the bookkeeping
// directory.
file_path
file_path::operator /(path_component const & to_append) const
{
  I(!to_append.empty());
  if (empty())
    {
      string const & s = to_append();
      I(!is_absolute_somewhere(s) && !in_bookkeeping_dir(s));
      return file_path(s, 0, string::npos);
    }
  else
    return file_path(((*(data.end() - 1) == '/') ? data : data + "/")
                     + to_append(), 0, string::npos);
}

// similarly, but even less checking is needed.
file_path
file_path::operator /(file_path const & to_append) const
{
  I(!to_append.empty());
  if (empty())
    return to_append;
  return file_path(((*(data.end() - 1) == '/') ? data : data + "/")
                   + to_append.as_internal(), 0, string::npos);
}

bookkeeping_path
bookkeeping_path::operator /(path_component const & to_append) const
{
  I(!to_append.empty());
  I(!empty());
  return bookkeeping_path(((*(data.end() - 1) == '/') ? data : data + "/")
                          + to_append(), 0, string::npos);
}

bookkeeping_path
bookkeeping_path::operator /(file_path const & to_append) const
{
  I(!to_append.empty());
  I(!empty());
  return bookkeeping_path(((*(data.end() - 1) == '/') ? data : data + "/")
                          + to_append.as_internal(), 0, string::npos);
}

system_path
system_path::operator /(path_component const & to_append) const
{
  I(!to_append.empty());
  I(!empty());
  return system_path(((*(data.end() - 1) == '/') ? data : data + "/")
                     + to_append(), 0, string::npos);
}

any_path
any_path::operator /(path_component const & to_append) const
{
  I(!to_append.empty());
  I(!empty());
  return any_path(((*(data.end() - 1) == '/') ? data : data + "/")
                  + to_append(), 0, string::npos);
}

// these take strings and validate
bookkeeping_path
bookkeeping_path::operator /(char const * to_append) const
{
  I(!is_absolute_somewhere(to_append));
  I(!empty());
  return bookkeeping_path(((*(data.end() - 1) == '/') ? data : data + "/")
                          + to_append, origin::internal);
}

system_path
system_path::operator /(char const * to_append) const
{
  I(!empty());
  I(!is_absolute_here(to_append));
  return system_path(((*(data.end() - 1) == '/') ? data : data + "/")
                     + to_append, origin::internal);
}

///////////////////////////////////////////////////////////////////////////
// system_path
///////////////////////////////////////////////////////////////////////////

system_path::system_path(any_path const & other, bool in_true_workspace)
{
  if (is_absolute_here(other.as_internal()))
    // another system_path.  the normalizing isn't really necessary, but it
    // makes me feel warm and fuzzy.
    data = normalize_path(other.as_internal());
  else
    {
      system_path wr;
      if (in_true_workspace)
        wr = working_root.get();
      else
        wr = working_root.get_but_unused();
      data = normalize_path(wr.as_internal() + "/" + other.as_internal());
    }
}

static inline string const_system_path(utf8 const & path)
{
  E(!path().empty(), path.made_from, F("invalid path ''"));
  string expanded = tilde_expand(path());
  if (is_absolute_here(expanded))
    return normalize_path(expanded);
  else
    return normalize_path(initial_abs_path.get().as_internal()
                          + "/" + path());
}

system_path::system_path(string const & path, origin::type from)
{
  data = const_system_path(utf8(path, from));
}

system_path::system_path(char const * const path)
{
  data = const_system_path(utf8(path, origin::internal));
}

system_path::system_path(utf8 const & path)
{
  data = const_system_path(utf8(path));
}

// Constant path predicates.
#define IMPLEMENT_CONST_PRED(cls, ret)                  \
  template <> bool                                      \
  path_always_##ret<cls>::operator()(cls const &) const \
  { return ret; }

IMPLEMENT_CONST_PRED(any_path, false)
IMPLEMENT_CONST_PRED(system_path, false)
IMPLEMENT_CONST_PRED(file_path, false)
IMPLEMENT_CONST_PRED(bookkeeping_path, false)

IMPLEMENT_CONST_PRED(any_path, true)
IMPLEMENT_CONST_PRED(system_path, true)
IMPLEMENT_CONST_PRED(file_path, true)
IMPLEMENT_CONST_PRED(bookkeeping_path, true)

#undef IMPLEMENT_CONST_PRED

// If this wasn't a user-supplied path, we should know
// which kind it is.
boost::shared_ptr<any_path>
new_optimal_path(std::string path, bool to_workspace_root)
{
  utf8 const utf8_path = utf8(path, origin::user);
  string normalized;
  try
    {
      normalize_external_path(utf8_path(), normalized, to_workspace_root);
    }
  catch (recoverable_failure &)
    {
      // not in workspace
      return boost::shared_ptr<any_path>(new system_path(path, origin::user));
    }

  if (in_bookkeeping_dir(normalized))
    return boost::shared_ptr<any_path>(new bookkeeping_path(normalized, origin::user));
  else
    return boost::shared_ptr<any_path>(new file_path(file_path_internal(normalized)));
};

// Either conversion of S to a path_component, or composition of P / S, has
// failed; figure out what went wrong and issue an appropriate diagnostic.

void
report_failed_path_composition(any_path const & p, char const * s,
                               bool isdir)
{
  utf8 badpth;
  if (p.empty())
    badpth = utf8(s);
  else
    badpth = utf8(p.as_internal() + "/" + s, p.made_from);
  if (bookkeeping_path::internal_string_is_bookkeeping_path(badpth))
    L(FL("ignoring bookkeeping directory '%s'") % badpth);
  else
    {
      // We rely on caller to tell us whether this is a directory.
      if (isdir)
        W(F("skipping directory '%s' with unsupported name") % badpth);
      else
        W(F("skipping file '%s' with unsupported name") % badpth);
    }
}

///////////////////////////////////////////////////////////////////////////
// workspace (and path root) handling
///////////////////////////////////////////////////////////////////////////

static bool
find_bookdir(system_path const & root, path_component const & bookdir,
             system_path & current, string & removed)
{
  current = initial_abs_path.get();
  removed.clear();

  // check that the current directory is below the specified search root
  if (current.as_internal().find(root.as_internal()) != 0)
    {
      W(F("current directory '%s' is not below root '%s'") % current % root);
      return false;
    }

  L(FL("searching for '%s' directory with root '%s'") % bookdir % root);

  system_path check;
  while (!(current == root))
    {
      check = current / bookdir;
      switch (get_path_status(check))
        {
        case path::nonexistent:
          L(FL("'%s' not found in '%s' with '%s' removed")
            % bookdir % current % removed);
          if (removed.empty())
            removed = current.basename()();
          else
            removed = current.basename()() + "/" + removed;
          current = current.dirname();
          continue;

        case path::file:
          L(FL("'%s' is not a directory") % check);
          return false;

        case path::directory:
          goto found;
        }
    }

  // if we get here, we have hit the root; try once more
  check = current / bookdir;
  switch (get_path_status(check))
    {
    case path::nonexistent:
      L(FL("'%s' not found in '%s' with '%s' removed")
        % bookdir % current % removed);
      return false;

    case path::file:
      L(FL("'%s' is not a directory") % check);
      return false;

    case path::directory:
      goto found;
    }
  return false;

 found:
  // check for _MTN/. and _MTN/.. to see if mt dir is readable
  try
    {
      if (!path_exists(check / ".") || !path_exists(check / ".."))
        {
          L(FL("problems with '%s' (missing '.' or '..')") % check);
          return false;
        }
    }
  catch(exception &)
    {
      L(FL("problems with '%s' (cannot check for '.' or '..')") % check);
      return false;
    }
  return true;
}


bool
find_and_go_to_workspace(string const & search_root)
{
  system_path root, current;
  string removed;

  if (search_root.empty())
    {
#ifdef WIN32
      std::string cur_str = get_current_working_dir();
      current = system_path(cur_str, origin::system);
      if (cur_str[0] == '/' || cur_str[0] == '\\')
        {
          if (cur_str.size() > 1 && (cur_str[1] == '/' || cur_str[1] == '\\'))
            {
              // UNC name
              string::size_type uncend = cur_str.find_first_of("\\/", 2);
              if (uncend == string::npos)
                root = system_path(cur_str + "/", origin::system);
              else
                root = system_path(cur_str.substr(0, uncend), origin::system);
            }
          else
            root = system_path("/");
        }
      else if (cur_str.size() > 1 && cur_str[1] == ':')
        {
          root = system_path(cur_str.substr(0,2) + "/", origin::system);
        }
      else I(false);
#else
      root = system_path("/", origin::internal);
#endif
    }
  else
    {
      root = system_path(search_root, origin::user);
      L(FL("limiting search for workspace to %s") % root);

      require_path_is_directory(root,
                               F("search root '%s' does not exist") % root,
                               F("search root '%s' is not a directory") % root);
    }

  // first look for the current name of the bookkeeping directory.
  // if we don't find it, look for it under the old name, so that
  // migration has a chance to work.
  if (!find_bookdir(root, bookkeeping_root_component, current, removed))
    if (!find_bookdir(root, old_bookkeeping_root_component, current, removed))
      return false;

  working_root.set(current, true);
  initial_rel_path.set(removed, true);

  L(FL("working root is '%s'") % working_root.get_but_unused());
  L(FL("initial relative path is '%s'") % initial_rel_path.get_but_unused());

  change_current_working_dir(working_root.get_but_unused());

  return true;
}

void
go_to_workspace(system_path const & new_workspace)
{
  working_root.set(new_workspace, true);
  initial_rel_path.set(string(), true);
  change_current_working_dir(new_workspace);
}

void
get_current_workspace(system_path & workspace)
{
  workspace = working_root.get_but_unused();
}

void
mark_std_paths_used(void)
{
  working_root.get();
  initial_rel_path.get();
}

///////////////////////////////////////////////////////////////////////////
// utility used by migrate_ancestry
///////////////////////////////////////////////////////////////////////////


static file_path
find_old_path_for(map<file_path, file_path> const & renames,
                  file_path const & new_path)
{
  map<file_path, file_path>::const_iterator i = renames.find(new_path);
  if (i != renames.end())
    return i->second;

  // ??? root directory rename possible in the old schema?
  // if not, do this first.
  if (new_path.empty())
    return new_path;

  file_path dir;
  path_component base;
  new_path.dirname_basename(dir, base);
  return find_old_path_for(renames, dir) / base;
}

file_path
find_new_path_for(map<file_path, file_path> const & renames,
                  file_path const & old_path)
{
  map<file_path, file_path> reversed;
  for (map<file_path, file_path>::const_iterator i = renames.begin();
       i != renames.end(); ++i)
    reversed.insert(make_pair(i->second, i->first));
  // this is a hackish kluge.  seems to work, though.
  return find_old_path_for(reversed, old_path);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
