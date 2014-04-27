// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __MIGRATION_HH__
#define __MIGRATION_HH__

// this file knows how to migrate schema databases. the general strategy is
// to hash each schema we ever use, and make a list of the SQL commands
// required to get from one hash value to the next. when you do a
// migration, the migrator locates your current db's state on the list and
// then runs all the migration functions between that point and the target
// of the migration.

#include <set>

struct sqlite3;
class key_store;
class database;
class project_t;
class system_path;

std::string describe_sql_schema(sqlite3 * db);
void check_sql_schema(sqlite3 * db, system_path const & filename);

// if you add a new item here, don't forget to raise the
// value of the "catch all" item "regen_all"
enum regen_cache_type { regen_none = 0, regen_rosters = 1,
                        regen_heights = 2, regen_branches = 4,
                        regen_file_sizes = 8, regen_all = 15 };

class migration_status {
  regen_cache_type _regen_type;
  std::string _flag_day_name;
public:
  migration_status() : _regen_type(regen_none), _flag_day_name("") {}
  explicit migration_status(regen_cache_type type, std::string flag_day_name = "")
    : _regen_type(type),
      _flag_day_name(flag_day_name)
  {}
  bool need_regen() const { return _regen_type != regen_none; }
  bool need_flag_day() const { return !_flag_day_name.empty(); }
  std::string flag_day_name() const { return _flag_day_name; }
  regen_cache_type regen_type() const { return _regen_type; }
};
migration_status migrate_sql_schema(sqlite3 * db, key_store & keys,
                                    system_path const & filename);

// utility routine shared with database.cc
void assert_sqlite3_ok(sqlite3 * db);

// debugging
void test_migration_step(sqlite3 * db, key_store & keys,
                         system_path const & filename,
                         std::string const & schema);

// this constant is part of the database schema, but it is not in schema.sql
// because sqlite expressions can't do arithmetic on character values.  it
// is stored in the "user version" field of the database header.  when we
// encounter a database whose schema hash we don't recognize, we look for
// this code in the header to decide whether it's a monotone database or
// some other sqlite3 database.  the expectation is that it will never need
// to change.  we call it a creator code because it has the same format and
// function as file creator codes in old-sk00l Mac OS.

const unsigned int mtn_creator_code = ((('_'*256 + 'M')*256 + 'T')*256 + 'N');



// migrations of ancestry format and so on

void
build_changesets_from_manifest_ancestry(database & db, key_store & keys,
                                        project_t & project,
                                        std::set<std::string> const & attrs_to_drop);

void
build_roster_style_revs_from_manifest_style_revs(database & db, key_store & keys,
                                                 project_t & project,
                                                 std::set<std::string> const & attrs_to_drop);


void
regenerate_caches(database & db, regen_cache_type type);


#endif // __MIGRATION__

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
