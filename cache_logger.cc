// Copyright (C) 2010 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "cache_logger.hh"

#include <fstream>

using std::ofstream;
using std::endl;
using std::string;

class cache_logger_impl
{
public:
  ofstream stream;

  explicit cache_logger_impl(string const & filename)
    : stream(filename.c_str())
  { }
};

cache_logger::cache_logger(string const & filename, int max_size)
  : max_size(max_size)
{
  if (!filename.empty())
    {
      _impl.reset(new cache_logger_impl(filename));
    }
}

void cache_logger::log_exists(bool exists, int position,
                              int item_count, int est_size) const
{
  if (_impl)
    {
      _impl->stream << "Exists: " << (exists?"ok":"missing")
                    << "; position: " << position
                    << "; count: " << item_count
                    << "; size: " << est_size << " of " << max_size
                    << endl;
    }
}

void cache_logger::log_touch(bool exists, int position,
                             int item_count, int est_size) const
{
  if (_impl)
    {
      _impl->stream << "Touch: " << (exists?"ok":"missing")
                    << "; position: " << position
                    << "; count: " << item_count
                    << "; size: " << est_size << " of " << max_size
                    << endl;
    }
}

void cache_logger::log_fetch(bool exists, int position,
                             int item_count, int est_size) const
{
  if (_impl)
    {
      _impl->stream << "Fetch: " << (exists?"ok":"missing")
                    << "; position: " << position
                    << "; count: " << item_count
                    << "; size: " << est_size << " of " << max_size
                    << endl;
    }
}

void cache_logger::log_insert(int items_removed,
                             int item_count, int est_size) const
{
  if (_impl)
    {
      _impl->stream << "Insert... "
                    << " dropped items: " << items_removed
                    << "; count: " << item_count
                    << "; size: " << est_size << " of " << max_size
                    << endl;
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
