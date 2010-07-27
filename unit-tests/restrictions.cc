// Copyright (C) 2005 Derek Scherger <derek@echologic.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "unit_tests.hh"
#include "restrictions.hh"
#include "constants.hh"
#include "file_io.hh"
#include "roster.hh"

using std::string;
using std::vector;

// f's and g's are files
// x's and y's are directories
// and this is rather painful

#define fp_root file_path_internal("")
#define fp_f file_path_internal("f")
#define fp_g file_path_internal("g")

#define fp_x file_path_internal("x")
#define fp_xf file_path_internal("x/f")
#define fp_xg file_path_internal("x/g")
#define fp_xx file_path_internal("x/x")
#define fp_xxf file_path_internal("x/x/f")
#define fp_xxg file_path_internal("x/x/g")
#define fp_xy file_path_internal("x/y")
#define fp_xyf file_path_internal("x/y/f")
#define fp_xyg file_path_internal("x/y/g")

#define fp_y file_path_internal("y")
#define fp_yf file_path_internal("y/f")
#define fp_yg file_path_internal("y/g")
#define fp_yx file_path_internal("y/x")
#define fp_yxf file_path_internal("y/x/f")
#define fp_yxg file_path_internal("y/x/g")
#define fp_yy file_path_internal("y/y")
#define fp_yyf file_path_internal("y/y/f")
#define fp_yyg file_path_internal("y/y/g")

namespace
{
  node_id nid_root;
  node_id nid_f;
  node_id nid_g;

  node_id nid_x;
  node_id nid_xf;
  node_id nid_xg;
  node_id nid_xx;
  node_id nid_xxf;
  node_id nid_xxg;
  node_id nid_xy;
  node_id nid_xyf;
  node_id nid_xyg;

  node_id nid_y;
  node_id nid_yf;
  node_id nid_yg;
  node_id nid_yx;
  node_id nid_yxf;
  node_id nid_yxg;
  node_id nid_yy;
  node_id nid_yyf;
  node_id nid_yyg;

  file_id fid_f  (string(constants::idlen_bytes, '\x11'), origin::internal);
  file_id fid_g  (string(constants::idlen_bytes, '\x22'), origin::internal);

  file_id fid_xf (string(constants::idlen_bytes, '\x33'), origin::internal);
  file_id fid_xg (string(constants::idlen_bytes, '\x44'), origin::internal);
  file_id fid_xxf(string(constants::idlen_bytes, '\x55'), origin::internal);
  file_id fid_xxg(string(constants::idlen_bytes, '\x66'), origin::internal);
  file_id fid_xyf(string(constants::idlen_bytes, '\x77'), origin::internal);
  file_id fid_xyg(string(constants::idlen_bytes, '\x88'), origin::internal);

  file_id fid_yf (string(constants::idlen_bytes, '\x99'), origin::internal);
  file_id fid_yg (string(constants::idlen_bytes, '\xaa'), origin::internal);
  file_id fid_yxf(string(constants::idlen_bytes, '\xbb'), origin::internal);
  file_id fid_yxg(string(constants::idlen_bytes, '\xcc'), origin::internal);
  file_id fid_yyf(string(constants::idlen_bytes, '\xdd'), origin::internal);
  file_id fid_yyg(string(constants::idlen_bytes, '\xee'), origin::internal);
}

static void setup(roster_t & roster)
{
  temp_node_id_source nis;

  // these directories must exist for the path_restrictions to be valid.
  mkdir_p(file_path_internal("x/x"));
  mkdir_p(file_path_internal("x/y"));
  mkdir_p(file_path_internal("y/x"));
  mkdir_p(file_path_internal("y/y"));

  nid_root = roster.create_dir_node(nis);
  nid_f    = roster.create_file_node(fid_f, nis);
  nid_g    = roster.create_file_node(fid_g, nis);

  nid_x   = roster.create_dir_node(nis);
  nid_xf  = roster.create_file_node(fid_xf, nis);
  nid_xg  = roster.create_file_node(fid_xg, nis);
  nid_xx  = roster.create_dir_node(nis);
  nid_xxf = roster.create_file_node(fid_xxf, nis);
  nid_xxg = roster.create_file_node(fid_xxg, nis);
  nid_xy  = roster.create_dir_node(nis);
  nid_xyf = roster.create_file_node(fid_xxf, nis);
  nid_xyg = roster.create_file_node(fid_xxg, nis);

  nid_y   = roster.create_dir_node(nis);
  nid_yf  = roster.create_file_node(fid_yf, nis);
  nid_yg  = roster.create_file_node(fid_yg, nis);
  nid_yx  = roster.create_dir_node(nis);
  nid_yxf = roster.create_file_node(fid_yxf, nis);
  nid_yxg = roster.create_file_node(fid_yxg, nis);
  nid_yy  = roster.create_dir_node(nis);
  nid_yyf = roster.create_file_node(fid_yxf, nis);
  nid_yyg = roster.create_file_node(fid_yxg, nis);

  roster.attach_node(nid_root, fp_root);
  roster.attach_node(nid_f, fp_f);
  roster.attach_node(nid_g, fp_g);

  roster.attach_node(nid_x,   fp_x);
  roster.attach_node(nid_xf,  fp_xf);
  roster.attach_node(nid_xg,  fp_xg);
  roster.attach_node(nid_xx,  fp_xx);
  roster.attach_node(nid_xxf, fp_xxf);
  roster.attach_node(nid_xxg, fp_xxg);
  roster.attach_node(nid_xy,  fp_xy);
  roster.attach_node(nid_xyf, fp_xyf);
  roster.attach_node(nid_xyg, fp_xyg);

  roster.attach_node(nid_y,   fp_y);
  roster.attach_node(nid_yf,  fp_yf);
  roster.attach_node(nid_yg,  fp_yg);
  roster.attach_node(nid_yx,  fp_yx);
  roster.attach_node(nid_yxf, fp_yxf);
  roster.attach_node(nid_yxg, fp_yxg);
  roster.attach_node(nid_yy,  fp_yy);
  roster.attach_node(nid_yyf, fp_yyf);
  roster.attach_node(nid_yyg, fp_yyg);

}

UNIT_TEST(empty_restriction)
{
  roster_t roster;
  setup(roster);

  // check restricted nodes

  node_restriction nmask;

  UNIT_TEST_CHECK(nmask.empty());

  UNIT_TEST_CHECK(nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_g));

  UNIT_TEST_CHECK(nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_xyg));

  UNIT_TEST_CHECK(nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK(nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask;

  UNIT_TEST_CHECK(pmask.empty());

  UNIT_TEST_CHECK(pmask.includes(fp_root));
  UNIT_TEST_CHECK(pmask.includes(fp_f));
  UNIT_TEST_CHECK(pmask.includes(fp_g));

  UNIT_TEST_CHECK(pmask.includes(fp_x));
  UNIT_TEST_CHECK(pmask.includes(fp_xf));
  UNIT_TEST_CHECK(pmask.includes(fp_xg));
  UNIT_TEST_CHECK(pmask.includes(fp_xx));
  UNIT_TEST_CHECK(pmask.includes(fp_xxf));
  UNIT_TEST_CHECK(pmask.includes(fp_xxg));
  UNIT_TEST_CHECK(pmask.includes(fp_xy));
  UNIT_TEST_CHECK(pmask.includes(fp_xyf));
  UNIT_TEST_CHECK(pmask.includes(fp_xyg));

  UNIT_TEST_CHECK(pmask.includes(fp_y));
  UNIT_TEST_CHECK(pmask.includes(fp_yf));
  UNIT_TEST_CHECK(pmask.includes(fp_yg));
  UNIT_TEST_CHECK(pmask.includes(fp_yx));
  UNIT_TEST_CHECK(pmask.includes(fp_yxf));
  UNIT_TEST_CHECK(pmask.includes(fp_yxg));
  UNIT_TEST_CHECK(pmask.includes(fp_yy));
  UNIT_TEST_CHECK(pmask.includes(fp_yyf));
  UNIT_TEST_CHECK(pmask.includes(fp_yyg));
}

UNIT_TEST(simple_include)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("x/x"));
  includes.push_back(file_path_internal("y/y"));

  // check restricted nodes

  node_restriction nmask(includes, excludes, -1, roster);

  UNIT_TEST_CHECK(!nmask.empty());

  // the root is included implicitly as the parent of x/x and y/y
  UNIT_TEST_CHECK( nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_g));

  // x is included implicitly as the parent of x/x
  UNIT_TEST_CHECK( nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyg));

  // y is included implicitly as the parent of y/y
  UNIT_TEST_CHECK( nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask(includes, excludes, -1);

  UNIT_TEST_CHECK(!pmask.empty());

  // the root is included implicitly as the parent of x/x and y/y
  UNIT_TEST_CHECK( pmask.includes(fp_root));
  UNIT_TEST_CHECK(!pmask.includes(fp_f));
  UNIT_TEST_CHECK(!pmask.includes(fp_g));

  // x is included implicitly as the parent of x/x
  UNIT_TEST_CHECK( pmask.includes(fp_x));
  UNIT_TEST_CHECK(!pmask.includes(fp_xf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xg));
  UNIT_TEST_CHECK( pmask.includes(fp_xx));
  UNIT_TEST_CHECK( pmask.includes(fp_xxf));
  UNIT_TEST_CHECK( pmask.includes(fp_xxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xy));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyg));

  // y is included implicitly as the parent of y/y
  UNIT_TEST_CHECK( pmask.includes(fp_y));
  UNIT_TEST_CHECK(!pmask.includes(fp_yf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yx));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxg));
  UNIT_TEST_CHECK( pmask.includes(fp_yy));
  UNIT_TEST_CHECK( pmask.includes(fp_yyf));
  UNIT_TEST_CHECK( pmask.includes(fp_yyg));
}

UNIT_TEST(simple_exclude)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  excludes.push_back(file_path_internal("x/x"));
  excludes.push_back(file_path_internal("y/y"));

  // check restricted nodes

  node_restriction nmask(includes, excludes, -1, roster);

  UNIT_TEST_CHECK(!nmask.empty());

  UNIT_TEST_CHECK( nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_g));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xyg));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask(includes, excludes, -1);

  UNIT_TEST_CHECK(!pmask.empty());

  UNIT_TEST_CHECK( pmask.includes(fp_root));
  UNIT_TEST_CHECK( pmask.includes(fp_f));
  UNIT_TEST_CHECK( pmask.includes(fp_g));

  UNIT_TEST_CHECK( pmask.includes(fp_x));
  UNIT_TEST_CHECK( pmask.includes(fp_xf));
  UNIT_TEST_CHECK( pmask.includes(fp_xg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xx));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxg));
  UNIT_TEST_CHECK( pmask.includes(fp_xy));
  UNIT_TEST_CHECK( pmask.includes(fp_xyf));
  UNIT_TEST_CHECK( pmask.includes(fp_xyg));

  UNIT_TEST_CHECK( pmask.includes(fp_y));
  UNIT_TEST_CHECK( pmask.includes(fp_yf));
  UNIT_TEST_CHECK( pmask.includes(fp_yg));
  UNIT_TEST_CHECK( pmask.includes(fp_yx));
  UNIT_TEST_CHECK( pmask.includes(fp_yxf));
  UNIT_TEST_CHECK( pmask.includes(fp_yxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yy));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyg));
}

UNIT_TEST(include_exclude)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("x"));
  includes.push_back(file_path_internal("y"));
  excludes.push_back(file_path_internal("x/x"));
  excludes.push_back(file_path_internal("y/y"));

  // check restricted nodes

  node_restriction nmask(includes, excludes, -1, roster);

  UNIT_TEST_CHECK(!nmask.empty());

  // the root is included implicitly as the parent of x and y
  UNIT_TEST_CHECK( nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_g));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xyg));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask(includes, excludes, -1);

  UNIT_TEST_CHECK(!pmask.empty());

  // the root is included implicitly as the parent of x and y
  UNIT_TEST_CHECK( pmask.includes(fp_root));
  UNIT_TEST_CHECK(!pmask.includes(fp_f));
  UNIT_TEST_CHECK(!pmask.includes(fp_g));

  UNIT_TEST_CHECK( pmask.includes(fp_x));
  UNIT_TEST_CHECK( pmask.includes(fp_xf));
  UNIT_TEST_CHECK( pmask.includes(fp_xg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xx));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxg));
  UNIT_TEST_CHECK( pmask.includes(fp_xy));
  UNIT_TEST_CHECK( pmask.includes(fp_xyf));
  UNIT_TEST_CHECK( pmask.includes(fp_xyg));

  UNIT_TEST_CHECK( pmask.includes(fp_y));
  UNIT_TEST_CHECK( pmask.includes(fp_yf));
  UNIT_TEST_CHECK( pmask.includes(fp_yg));
  UNIT_TEST_CHECK( pmask.includes(fp_yx));
  UNIT_TEST_CHECK( pmask.includes(fp_yxf));
  UNIT_TEST_CHECK( pmask.includes(fp_yxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yy));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyg));
}

UNIT_TEST(exclude_include)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  // note that excludes higher up the tree than the top
  // include are rather pointless -- nothing above the
  // top include is included anyway
  excludes.push_back(file_path_internal("x"));
  excludes.push_back(file_path_internal("y"));
  includes.push_back(file_path_internal("x/x"));
  includes.push_back(file_path_internal("y/y"));

  // check restricted nodes

  node_restriction nmask(includes, excludes, -1, roster);

  UNIT_TEST_CHECK(!nmask.empty());

  // the root is included implicitly as the parent of x/x and y/y
  UNIT_TEST_CHECK( nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_g));

  // x is included implicitly as the parent of x/x
  // even though x is also explcitly excluded
  // the implicit include applies only to x but not
  // its children
  UNIT_TEST_CHECK( nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyg));

  // y is included implicitly as the parent of y/y
  // even though y is also explcitly excluded
  // the implicit include applies only to y but not
  // its children
  UNIT_TEST_CHECK( nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask(includes, excludes, -1);

  UNIT_TEST_CHECK(!pmask.empty());

  // the root is included implicitly as the parent of x/x and y/y
  UNIT_TEST_CHECK( pmask.includes(fp_root));
  UNIT_TEST_CHECK(!pmask.includes(fp_f));
  UNIT_TEST_CHECK(!pmask.includes(fp_g));

  // x is included implicitly as the parent of x/x
  // even though x is also explcitly excluded
  // the implicit include applies only to x but not
  // its children
  UNIT_TEST_CHECK( pmask.includes(fp_x));
  UNIT_TEST_CHECK(!pmask.includes(fp_xf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xg));
  UNIT_TEST_CHECK( pmask.includes(fp_xx));
  UNIT_TEST_CHECK( pmask.includes(fp_xxf));
  UNIT_TEST_CHECK( pmask.includes(fp_xxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xy));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyg));

  // y is included implicitly as the parent of y/y
  // even though y is also explcitly excluded
  // the implicit include applies only to y but not
  // its children
  UNIT_TEST_CHECK( pmask.includes(fp_y));
  UNIT_TEST_CHECK(!pmask.includes(fp_yf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yx));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxg));
  UNIT_TEST_CHECK( pmask.includes(fp_yy));
  UNIT_TEST_CHECK( pmask.includes(fp_yyf));
  UNIT_TEST_CHECK( pmask.includes(fp_yyg));
}

UNIT_TEST(invalid_roster_paths)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("foo"));
  excludes.push_back(file_path_internal("bar"));

  UNIT_TEST_CHECK_THROW(node_restriction(includes, excludes, -1, roster),
                        recoverable_failure);
}

UNIT_TEST(invalid_workspace_paths)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("foo"));
  excludes.push_back(file_path_internal("bar"));

  UNIT_TEST_CHECK_THROW(path_restriction(includes, excludes, -1),
                        recoverable_failure);
}

UNIT_TEST(ignored_invalid_workspace_paths)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("foo"));
  excludes.push_back(file_path_internal("bar"));

  path_restriction pmask(includes, excludes, -1,
                         path_restriction::skip_check);

  UNIT_TEST_CHECK( pmask.includes(file_path_internal("foo")));
  UNIT_TEST_CHECK(!pmask.includes(file_path_internal("bar")));
}

UNIT_TEST(include_depth_0)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("x"));
  includes.push_back(file_path_internal("y"));

  long depth = 0;

  // check restricted nodes

  node_restriction nmask(includes, excludes, depth, roster);

  UNIT_TEST_CHECK(!nmask.empty());

  // root is included implicitly as the parent of x and y
  UNIT_TEST_CHECK( nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_g));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyg));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask(includes, excludes, depth);

  UNIT_TEST_CHECK(!pmask.empty());

  // root is included implicitly as the parent of x and y
  UNIT_TEST_CHECK( pmask.includes(fp_root));
  UNIT_TEST_CHECK(!pmask.includes(fp_f));
  UNIT_TEST_CHECK(!pmask.includes(fp_g));

  UNIT_TEST_CHECK( pmask.includes(fp_x));
  UNIT_TEST_CHECK(!pmask.includes(fp_xf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xx));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xy));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyg));

  UNIT_TEST_CHECK( pmask.includes(fp_y));
  UNIT_TEST_CHECK(!pmask.includes(fp_yf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yx));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yy));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyg));
}

UNIT_TEST(include_depth_1)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("x"));
  includes.push_back(file_path_internal("y"));

  long depth = 1;

  // check restricted nodes

  node_restriction nmask(includes, excludes, depth, roster);

  UNIT_TEST_CHECK(!nmask.empty());

  // root is included implicitly as the parent of x and y
  UNIT_TEST_CHECK( nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_g));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyg));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask(includes, excludes, depth);

  UNIT_TEST_CHECK(!pmask.empty());

  // root is included implicitly as the parent of x and y
  UNIT_TEST_CHECK( pmask.includes(fp_root));
  UNIT_TEST_CHECK(!pmask.includes(fp_f));
  UNIT_TEST_CHECK(!pmask.includes(fp_g));

  UNIT_TEST_CHECK( pmask.includes(fp_x));
  UNIT_TEST_CHECK( pmask.includes(fp_xf));
  UNIT_TEST_CHECK( pmask.includes(fp_xg));
  UNIT_TEST_CHECK( pmask.includes(fp_xx));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxg));
  UNIT_TEST_CHECK( pmask.includes(fp_xy));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyg));

  UNIT_TEST_CHECK( pmask.includes(fp_y));
  UNIT_TEST_CHECK( pmask.includes(fp_yf));
  UNIT_TEST_CHECK( pmask.includes(fp_yg));
  UNIT_TEST_CHECK( pmask.includes(fp_yx));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxg));
  UNIT_TEST_CHECK( pmask.includes(fp_yy));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyg));
}

UNIT_TEST(include_depth_1_empty_restriction)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;

  long depth = 1;

  // check restricted nodes

  node_restriction nmask(includes, excludes, depth, roster);

  UNIT_TEST_CHECK( nmask.empty());

  UNIT_TEST_CHECK( nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_g));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_xyg));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask(includes, excludes, depth);

  UNIT_TEST_CHECK( pmask.empty());

  UNIT_TEST_CHECK( pmask.includes(fp_root));
  UNIT_TEST_CHECK( pmask.includes(fp_f));
  UNIT_TEST_CHECK( pmask.includes(fp_g));

  UNIT_TEST_CHECK( pmask.includes(fp_x));
  UNIT_TEST_CHECK(!pmask.includes(fp_xf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xx));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_xy));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_xyg));

  UNIT_TEST_CHECK( pmask.includes(fp_y));
  UNIT_TEST_CHECK(!pmask.includes(fp_yf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yx));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yxg));
  UNIT_TEST_CHECK(!pmask.includes(fp_yy));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyf));
  UNIT_TEST_CHECK(!pmask.includes(fp_yyg));
}

UNIT_TEST(include_depth_2)
{
  roster_t roster;
  setup(roster);

  vector<file_path> includes, excludes;
  includes.push_back(file_path_internal("x"));
  includes.push_back(file_path_internal("y"));

  long depth = 2;

  // check restricted nodes

  node_restriction nmask(includes, excludes, depth, roster);

  UNIT_TEST_CHECK(!nmask.empty());

  // root is included implicitly as the parent of x and y
  UNIT_TEST_CHECK( nmask.includes(roster, nid_root));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_f));
  UNIT_TEST_CHECK(!nmask.includes(roster, nid_g));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_x));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xx));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xxf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xy));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xyf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_xyg));

  UNIT_TEST_CHECK( nmask.includes(roster, nid_y));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yx));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yxf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yxg));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yy));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yyf));
  UNIT_TEST_CHECK( nmask.includes(roster, nid_yyg));

  // check restricted paths

  path_restriction pmask(includes, excludes, depth);

  UNIT_TEST_CHECK(!pmask.empty());

  // root is included implicitly as the parent of x and y
  UNIT_TEST_CHECK( pmask.includes(fp_root));
  UNIT_TEST_CHECK(!pmask.includes(fp_f));
  UNIT_TEST_CHECK(!pmask.includes(fp_g));

  UNIT_TEST_CHECK( pmask.includes(fp_x));
  UNIT_TEST_CHECK( pmask.includes(fp_xf));
  UNIT_TEST_CHECK( pmask.includes(fp_xg));
  UNIT_TEST_CHECK( pmask.includes(fp_xx));
  UNIT_TEST_CHECK( pmask.includes(fp_xxf));
  UNIT_TEST_CHECK( pmask.includes(fp_xxg));
  UNIT_TEST_CHECK( pmask.includes(fp_xy));
  UNIT_TEST_CHECK( pmask.includes(fp_xyf));
  UNIT_TEST_CHECK( pmask.includes(fp_xyg));

  UNIT_TEST_CHECK( pmask.includes(fp_y));
  UNIT_TEST_CHECK( pmask.includes(fp_yf));
  UNIT_TEST_CHECK( pmask.includes(fp_yg));
  UNIT_TEST_CHECK( pmask.includes(fp_yx));
  UNIT_TEST_CHECK( pmask.includes(fp_yxf));
  UNIT_TEST_CHECK( pmask.includes(fp_yxg));
  UNIT_TEST_CHECK( pmask.includes(fp_yy));
  UNIT_TEST_CHECK( pmask.includes(fp_yyf));
  UNIT_TEST_CHECK( pmask.includes(fp_yyg));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
