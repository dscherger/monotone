// Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __PLATFORM_WRAPPED_HH__
#define __PLATFORM_WRAPPED_HH__

#include "paths.hh"
#include "platform.hh"
#include "vocab.hh"
#include "vector.hh"

inline void change_current_working_dir(any_path const & to)
{
  change_current_working_dir(to.as_external());
}

inline path::status get_path_status(any_path const & path)
{
  std::string p(path.as_external());
  return get_path_status(p.empty()?".":p);
}

inline void rename_clobberingly(any_path const & from, any_path const & to)
{
  rename_clobberingly(from.as_external(), to.as_external());
}

// Some generally-useful dirent consumers

struct dirent_ignore : public dirent_consumer
{
  virtual void consume(char const *) {}
};

template <class T>
struct fill_path_vec : public dirent_consumer
{
  fill_path_vec(T const & parent, std::vector<T> & v, bool isdir)
    : parent(parent), v(v), isdir(isdir)
  { v.clear(); }

  virtual void consume(char const * s)
  {
    T result;
    if (safe_compose(parent, s, result, isdir))
      v.push_back(result);
  }
private:
  T const & parent;
  std::vector<T> & v;
  bool isdir;
};

struct special_file_error : public dirent_consumer
{
  special_file_error(any_path const & p) : parent(p) {}
  virtual void consume(char const * f)
  {
    any_path result;
    if (safe_compose(parent, f, result, false))
      E(false, origin::system,
        F("'%s' is neither a file nor a directory") % result);
  }
private:
  any_path const & parent;
};

inline void
do_read_directory(any_path const & path,
                  dirent_consumer & files,
                  dirent_consumer & dirs,
                  dirent_consumer & specials)
{
  do_read_directory(path.as_external(), files, dirs, specials);
}

inline void
do_read_directory(any_path const & path,
                  dirent_consumer & files,
                  dirent_consumer & dirs)
{
  special_file_error sfe(path);
  do_read_directory(path.as_external(), files, dirs, sfe);
}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
