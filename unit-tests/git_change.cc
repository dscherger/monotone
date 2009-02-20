// Copyright (C) 2009 Derek Scherger <derek@echologic.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "unit_tests.hh"
#include "git_change.hh"

using std::make_pair;
using std::vector;

UNIT_TEST(reorder_chained_renames)
{
  vector<git_rename> renames, reordered_renames;
  renames.push_back(make_pair(file_path_internal("a"), file_path_internal("b")));
  renames.push_back(make_pair(file_path_internal("b"), file_path_internal("c")));
  renames.push_back(make_pair(file_path_internal("c"), file_path_internal("d")));

  // these should be reordered from a->b b->c c->d to c->d b->c a->b
  reorder_renames(renames, reordered_renames);
  rename_iterator rename = reordered_renames.begin();
  UNIT_TEST_CHECK(rename->first == file_path_internal("c"));
  UNIT_TEST_CHECK(rename->second == file_path_internal("d"));
  ++rename;
  UNIT_TEST_CHECK(rename->first == file_path_internal("b"));
  UNIT_TEST_CHECK(rename->second == file_path_internal("c"));
  ++rename;
  UNIT_TEST_CHECK(rename->first == file_path_internal("a"));
  UNIT_TEST_CHECK(rename->second == file_path_internal("b"));
  ++rename;
  UNIT_TEST_CHECK(rename == reordered_renames.end());
}

UNIT_TEST(reorder_swapped_renames)
{
  vector<git_rename> renames, reordered_renames;
  renames.push_back(make_pair(file_path_internal("a"), file_path_internal("b")));
  renames.push_back(make_pair(file_path_internal("b"), file_path_internal("a")));

  // these should be reordered from a->b b->a to a->tmp b->a tmp->b
  reorder_renames(renames, reordered_renames);
  rename_iterator rename = reordered_renames.begin();
  UNIT_TEST_CHECK(rename->first == file_path_internal("a"));
  UNIT_TEST_CHECK(rename->second == file_path_internal("a.tmp.break-rename-loop"));
  ++rename;
  UNIT_TEST_CHECK(rename->first == file_path_internal("b"));
  UNIT_TEST_CHECK(rename->second == file_path_internal("a"));
  ++rename;
  UNIT_TEST_CHECK(rename->first == file_path_internal("a.tmp.break-rename-loop"));
  UNIT_TEST_CHECK(rename->second == file_path_internal("b"));
  ++rename;
  UNIT_TEST_CHECK(rename == reordered_renames.end());
}

UNIT_TEST(reorder_rename_loop)
{
  vector<git_rename> renames, reordered_renames;
  renames.push_back(make_pair(file_path_internal("a"), file_path_internal("b")));
  renames.push_back(make_pair(file_path_internal("b"), file_path_internal("c")));
  renames.push_back(make_pair(file_path_internal("c"), file_path_internal("a")));

  // these should be reordered from a->b b->c c->a to a->tmp c->a b->c a->b tmp->b
  reorder_renames(renames, reordered_renames);
  rename_iterator rename = reordered_renames.begin();
  UNIT_TEST_CHECK(rename->first == file_path_internal("a"));
  UNIT_TEST_CHECK(rename->second == file_path_internal("a.tmp.break-rename-loop"));
  ++rename;
  UNIT_TEST_CHECK(rename->first == file_path_internal("c"));
  UNIT_TEST_CHECK(rename->second == file_path_internal("a"));
  ++rename;
  UNIT_TEST_CHECK(rename->first == file_path_internal("b"));
  UNIT_TEST_CHECK(rename->second == file_path_internal("c"));
  ++rename;
  UNIT_TEST_CHECK(rename->first == file_path_internal("a.tmp.break-rename-loop"));
  UNIT_TEST_CHECK(rename->second == file_path_internal("b"));
  ++rename;
  UNIT_TEST_CHECK(rename == reordered_renames.end());
}

UNIT_TEST(reorder_reversed_rename_loop)
{
  vector<git_rename> renames, reordered_renames;
  renames.push_back(make_pair(file_path_internal("z"), file_path_internal("y")));
  renames.push_back(make_pair(file_path_internal("y"), file_path_internal("x")));
  renames.push_back(make_pair(file_path_internal("x"), file_path_internal("z")));

  // assuming that the x->z rename gets pulled from the rename map first
  // these should be reordered from z->y y->x x->z to x->tmp y->x z->y tmp->z
  reorder_renames(renames, reordered_renames);
  rename_iterator rename = reordered_renames.begin();
  UNIT_TEST_CHECK(rename->first == file_path_internal("x"));
  UNIT_TEST_CHECK(rename->second == file_path_internal("x.tmp.break-rename-loop"));
  ++rename;
  UNIT_TEST_CHECK(rename->first == file_path_internal("y"));
  UNIT_TEST_CHECK(rename->second == file_path_internal("x"));
  ++rename;
  UNIT_TEST_CHECK(rename->first == file_path_internal("z"));
  UNIT_TEST_CHECK(rename->second == file_path_internal("y"));
  ++rename;
  UNIT_TEST_CHECK(rename->first == file_path_internal("x.tmp.break-rename-loop"));
  UNIT_TEST_CHECK(rename->second == file_path_internal("z"));
  ++rename;
  UNIT_TEST_CHECK(rename == reordered_renames.end());
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
