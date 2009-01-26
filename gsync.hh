#ifndef __GSYNC_HH__
#define __GSYNC_HH__

// Copyright (C) 2008 Markus Schiltknecht <markus@bluegap.ch>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"

#include <set>

#include "vocab.hh"

struct uri;
struct globish;

struct file_data_record
{
  file_id id;
  file_data dat;
  file_data_record(file_id id, file_data dat) :
    id(id), dat(dat) {}
};

struct file_delta_record
{
  file_id src_id;
  file_id dst_id;
  file_delta del;
  file_delta_record(file_id src_id, file_id dst_id, file_delta del) :
    src_id(src_id), dst_id(dst_id), del(del) {}
};

class lua_hooks;
class database;
class revision_t;

class
channel
{
public:
  virtual void inquire_about_revs(std::set<revision_id> const & query_set,
                                  std::set<revision_id> & theirs) const = 0;
  virtual void get_descendants(std::set<revision_id> const & common_revs,
                               std::vector<revision_id> & inbound_revs) const = 0;

  virtual void push_full_rev(revision_id const & rid,
                             revision_t const & rev,
                             std::vector<file_data_record> const & data_records,
                             std::vector<file_delta_record> const & delta_records) const = 0;

  virtual void pull_full_rev(revision_id const & rid,
                             revision_t & rev,
                             std::vector<file_data_record> & data_records,
                             std::vector<file_delta_record> & delta_records) const = 0;

  virtual void push_file_data(file_id const & id,
                              file_data const & data) const = 0;
  virtual void push_file_delta(file_id const & old_id,
                               file_id const & new_id,
                               file_delta const & delta) const = 0;

  virtual void push_rev(revision_id const & rid, revision_t const & rev) const = 0;
  virtual void pull_rev(revision_id const & rid, revision_t & rev) const = 0;

  virtual void pull_file_data(file_id const & id,
                              file_data & data) const = 0;
  virtual void pull_file_delta(file_id const & old_id,
                               file_id const & new_id,
                               file_delta & delta) const = 0;

  virtual ~channel() {}
};

extern void
run_gsync_protocol(lua_hooks & lua, database & db, channel const & ch,
                   bool const dryrun);

void
load_full_rev(database & db,
              revision_id const rid,
              revision_t & rev,
              std::vector<file_data_record> & data_records,
              std::vector<file_delta_record> & delta_records);

void
store_full_rev(database & db,
               revision_id const rid,
               revision_t const & rev,
               std::vector<file_data_record> const & data_records,
               std::vector<file_delta_record> const & delta_records);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __GSYNC_HH__
