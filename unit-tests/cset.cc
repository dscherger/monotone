// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "unit_tests.hh"
#include "transforms.hh"
#include "cset.hh"
#include "roster.hh"

using std::logic_error;
using std::make_pair;
using std::string;

static void
setup_roster(roster_t & r, file_id const & fid, node_id_source & nis)
{
  // sets up r to have a root dir, a dir in it name "foo", and a file under
  // that named "bar", and the file has the given id.
  // the file has attr "attr_file=value_file", and the dir has
  // "attr_dir=value_dir".
  r = roster_t();

  {
    r.attach_node(r.create_dir_node(nis), file_path_internal(""));
  }
  {
    file_path fp = file_path_internal("foo");
    r.attach_node(r.create_dir_node(nis), fp);
    r.set_attr(fp, attr_key("attr_dir"), attr_value("value_dir"));
  }
  {
    file_path fp = file_path_internal("foo/bar");
    r.attach_node(r.create_file_node(fid, nis), fp);
    r.set_attr(fp, attr_key("attr_file"), attr_value("value_file"));
  }
}

UNIT_TEST(cset_written)
{
  {
    L(FL("TEST: cset reading - operation misordering"));
    // bad cset, add_dir should be before add_file
    string s("delete \"foo\"\n"
             "\n"
             "rename \"quux\"\n"
             "    to \"baz\"\n"
             "\n"
             "add_file \"bar\"\n"
             " content [0000000000000000000000000000000000000000]\n"
             "\n"
             "add_dir \"pling\"\n");
    data d1(s, origin::internal);
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(d1, cs), logic_error);
    // check that it still fails if there's extra stanzas past the
    // mis-ordered entries
    data d2(s + "\n"
                "  set \"bar\"\n"
                " attr \"flavoursome\"\n"
                "value \"mostly\"\n",
            origin::internal);
    UNIT_TEST_CHECK_THROW(read_cset(d2, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in delete"));
    // bad cset, bar should be before foo
    data dat("delete \"foo\"\n"
             "\n"
             "delete \"bar\"\n");
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in rename"));
    // bad cset, bar should be before foo
    data dat("rename \"foo\"\n"
             "    to \"foonew\"\n"
             "\n"
             "rename \"bar\"\n"
             "    to \"barnew\"\n");
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in add_dir"));
    // bad cset, bar should be before foo
    data dat("add_dir \"foo\"\n"
             "\n"
             "add_dir \"bar\"\n");
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in add_file"));
    // bad cset, bar should be before foo
    data dat("add_file \"foo\"\n"
             " content [0000000000000000000000000000000000000000]\n"
             "\n"
             "add_file \"bar\"\n"
             " content [0000000000000000000000000000000000000000]\n");
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in add_file"));
    // bad cset, bar should be before foo
    data dat("add_file \"foo\"\n"
             " content [0000000000000000000000000000000000000000]\n"
             "\n"
             "add_file \"bar\"\n"
             " content [0000000000000000000000000000000000000000]\n");
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in patch"));
    // bad cset, bar should be before foo
    data dat("patch \"foo\"\n"
             " from [0000000000000000000000000000000000000000]\n"
             "   to [1000000000000000000000000000000000000000]\n"
             "\n"
             "patch \"bar\"\n"
             " from [0000000000000000000000000000000000000000]\n"
             "   to [1000000000000000000000000000000000000000]\n");
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in clear"));
    // bad cset, bar should be before foo
    data dat("clear \"foo\"\n"
             " attr \"flavoursome\"\n"
             "\n"
             "clear \"bar\"\n"
             " attr \"flavoursome\"\n");
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - misordered files in set"));
    // bad cset, bar should be before foo
    data dat("  set \"foo\"\n"
             " attr \"flavoursome\"\n"
             "value \"yes\"\n"
             "\n"
             "  set \"bar\"\n"
             " attr \"flavoursome\"\n"
             "value \"yes\"\n");
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - duplicate entries"));
    data dat("delete \"foo\"\n"
             "\n"
             "delete \"foo\"\n");
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - multiple different attrs"));
    // should succeed
    data dat( "  set \"bar\"\n"
              " attr \"flavoursome\"\n"
              "value \"mostly\"\n"
              "\n"
              "  set \"bar\"\n"
              " attr \"smell\"\n"
              "value \"socks\"\n");
    cset cs;
    UNIT_TEST_CHECK_NOT_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - wrong attr ordering in clear"));
    // fooish should be before quuxy
    data dat( "clear \"bar\"\n"
              " attr \"quuxy\"\n"
              "\n"
              "clear \"bar\"\n"
              " attr \"fooish\"\n");
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - wrong attr ordering in set"));
    // fooish should be before quuxy
    data dat( "  set \"bar\"\n"
              " attr \"quuxy\"\n"
              "value \"mostly\"\n"
              "\n"
              "  set \"bar\"\n"
              " attr \"fooish\"\n"
              "value \"seldom\"\n");
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset reading - duplicate attrs"));
    // can't have dups.
    data dat( "  set \"bar\"\n"
              " attr \"flavoursome\"\n"
              "value \"mostly\"\n"
              "\n"
              "  set \"bar\"\n"
              " attr \"flavoursome\"\n"
              "value \"sometimes\"\n");
    cset cs;
    UNIT_TEST_CHECK_THROW(read_cset(dat, cs), logic_error);
  }

  {
    L(FL("TEST: cset writing - normalisation"));
    cset cs; MM(cs);
    file_id f1(decode_hexenc_as<file_id>("1234567800000000000000000000000000000000",
                                         origin::internal));
    file_id f2(decode_hexenc_as<file_id>("9876543212394657263900000000000000000000",
                                         origin::internal));
    file_id f3(decode_hexenc_as<file_id>("0000000000011111111000000000000000000000",
                                         origin::internal));

    file_path foo = file_path_internal("foo");
    file_path foo_quux = file_path_internal("foo/quux");
    file_path bar = file_path_internal("bar");
    file_path quux = file_path_internal("quux");
    file_path idle = file_path_internal("idle");
    file_path fish = file_path_internal("fish");
    file_path womble = file_path_internal("womble");
    file_path policeman = file_path_internal("policeman");

    cs.dirs_added.insert(foo_quux);
    cs.dirs_added.insert(foo);
    cs.files_added.insert(make_pair(bar, f1));
    cs.nodes_deleted.insert(quux);
    cs.nodes_deleted.insert(idle);
    cs.nodes_renamed.insert(make_pair(fish, womble));
    cs.deltas_applied.insert(make_pair(womble, make_pair(f2, f3)));
    cs.attrs_cleared.insert(make_pair(policeman, attr_key("yodel")));
    cs.attrs_set.insert(make_pair(make_pair(policeman,
                        attr_key("axolotyl")), attr_value("fruitily")));
    cs.attrs_set.insert(make_pair(make_pair(policeman,
                        attr_key("spin")), attr_value("capybara")));

    data dat; MM(dat);
    write_cset(cs, dat);
    data expected("delete \"idle\"\n"
                  "\n"
                  "delete \"quux\"\n"
                  "\n"
                  "rename \"fish\"\n"
                  "    to \"womble\"\n"
                  "\n"
                  "add_dir \"foo\"\n"
                  "\n"
                  "add_dir \"foo/quux\"\n"
                  "\n"
                  "add_file \"bar\"\n"
                  " content [1234567800000000000000000000000000000000]\n"
                  "\n"
                  "patch \"womble\"\n"
                  " from [9876543212394657263900000000000000000000]\n"
                  "   to [0000000000011111111000000000000000000000]\n"
                  "\n"
                  "clear \"policeman\"\n"
                  " attr \"yodel\"\n"
                  "\n"
                  "  set \"policeman\"\n"
                  " attr \"axolotyl\"\n"
                  "value \"fruitily\"\n"
                  "\n"
                  "  set \"policeman\"\n"
                  " attr \"spin\"\n"
                  "value \"capybara\"\n"
                 );
    MM(expected);
    // I() so that it'll dump on failure
    UNIT_TEST_CHECK_NOT_THROW(I(expected == dat), logic_error);
  }
}

UNIT_TEST(basic_csets)
{

  temp_node_id_source nis;
  roster_t r;
  MM(r);

  editable_roster_base tree(r, nis);

  file_id f1(decode_hexenc_as<file_id>("0000000000000000000000000000000000000001",
                                       origin::internal));
  file_id f2(decode_hexenc_as<file_id>("0000000000000000000000000000000000000002",
                                       origin::internal));

  file_path root;
  file_path foo = file_path_internal("foo");
  file_path foo_bar = file_path_internal("foo/bar");
  file_path baz = file_path_internal("baz");
  file_path quux = file_path_internal("quux");

  // some basic tests that should succeed
  {
    L(FL("TEST: cset add file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.files_added.insert(make_pair(baz, f2));
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK(is_file_t(r.get_node(baz)));
    UNIT_TEST_CHECK(downcast_to_file_t(r.get_node(baz))->content == f2);
    UNIT_TEST_CHECK(r.all_nodes().size() == 4);
  }

  {
    L(FL("TEST: cset add dir"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.dirs_added.insert(quux);
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK(is_dir_t(r.get_node(quux)));
    UNIT_TEST_CHECK(r.all_nodes().size() == 4);
  }

  {
    L(FL("TEST: cset delete"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(foo_bar);
    cs.nodes_deleted.insert(foo);
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK(r.all_nodes().size() == 1); // only the root left
  }

  {
    L(FL("TEST: cset rename file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_renamed.insert(make_pair(foo_bar, quux));
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK(is_file_t(r.get_node(quux)));
    UNIT_TEST_CHECK(is_dir_t(r.get_node(foo)));
    UNIT_TEST_CHECK(!r.has_node(foo_bar));
    UNIT_TEST_CHECK(r.all_nodes().size() == 3);
  }

  {
    L(FL("TEST: cset rename dir"));
    file_path quux_bar = file_path_internal("quux/bar");
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_renamed.insert(make_pair(foo, quux));
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK(is_dir_t(r.get_node(quux)));
    UNIT_TEST_CHECK(is_file_t(r.get_node(quux_bar)));
    UNIT_TEST_CHECK(!r.has_node(foo));
    UNIT_TEST_CHECK(r.all_nodes().size() == 3);
  }

  {
    L(FL("TEST: patch file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.deltas_applied.insert(make_pair(foo_bar, make_pair(f1, f2)));
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK(is_dir_t(r.get_node(foo)));
    UNIT_TEST_CHECK(is_file_t(r.get_node(foo_bar)));
    UNIT_TEST_CHECK(downcast_to_file_t(r.get_node(foo_bar))->content == f2);
    UNIT_TEST_CHECK(r.all_nodes().size() == 3);
  }

  {
    L(FL("TEST: set attr"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_set.insert(make_pair(make_pair(foo_bar, attr_key("ping")),
                                  attr_value("klang")));
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);

    attr_map_t attrs = (r.get_node(foo_bar))->attrs;
    UNIT_TEST_CHECK(attrs[attr_key("ping")] == make_pair(true, attr_value("klang")));

    attrs = (r.get_node(foo))->attrs;
    UNIT_TEST_CHECK(attrs[attr_key("attr_dir")] == make_pair(true, attr_value("value_dir")));

    UNIT_TEST_CHECK(r.all_nodes().size() == 3);
  }

  {
    L(FL("TEST: clear attr file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_set.insert(make_pair(make_pair(foo_bar, attr_key("ping")),
                                  attr_value("klang")));
    cs.attrs_cleared.insert(make_pair(foo_bar, attr_key("attr_file")));
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK((r.get_node(foo_bar))->attrs[attr_key("attr_file")]
                == make_pair(false, attr_value("")));
    UNIT_TEST_CHECK(r.all_nodes().size() == 3);
  }

  // some renaming tests
  {
    L(FL("TEST: renaming at different levels"));
    setup_roster(r, f1, nis);

    file_path quux_bar = file_path_internal("quux/bar");
    file_path foo_bar = file_path_internal("foo/bar");
    file_path quux_sub = file_path_internal("quux/sub");
    file_path foo_sub = file_path_internal("foo/sub");
    file_path foo_sub_thing = file_path_internal("foo/sub/thing");
    file_path quux_sub_thing = file_path_internal("quux/sub/thing");
    file_path foo_sub_deep = file_path_internal("foo/sub/deep");
    file_path foo_subsub = file_path_internal("foo/subsub");
    file_path foo_subsub_deep = file_path_internal("foo/subsub/deep");

    { // build a tree
      cset cs; MM(cs);
      cs.dirs_added.insert(quux);
      cs.dirs_added.insert(quux_sub);
      cs.dirs_added.insert(foo_sub);
      cs.files_added.insert(make_pair(foo_sub_deep, f2));
      cs.files_added.insert(make_pair(quux_sub_thing, f1));
      UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
      UNIT_TEST_CHECK(r.all_nodes().size() == 8);
    }

    { // some renames
      cset cs; MM(cs);
      cs.nodes_renamed.insert(make_pair(foo, quux));
      cs.nodes_renamed.insert(make_pair(quux, foo));
      cs.nodes_renamed.insert(make_pair(foo_sub, foo_subsub));
      UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    }

    UNIT_TEST_CHECK(r.all_nodes().size() == 8);
    // /foo/bar -> /quux/bar
    UNIT_TEST_CHECK(is_file_t(r.get_node(quux_bar)));
    UNIT_TEST_CHECK(!(r.has_node(foo_bar)));
    // /foo/sub/deep -> /foo/subsub/deep
    UNIT_TEST_CHECK(is_file_t(r.get_node(foo_subsub_deep)));
    UNIT_TEST_CHECK(!(r.has_node(foo_sub_deep)));
    // /quux/sub -> /foo/sub
    UNIT_TEST_CHECK(is_dir_t(r.get_node(foo_sub)));
    UNIT_TEST_CHECK(!(r.has_node(quux_sub)));
    // /quux/sub/thing -> /foo/sub/thing
    UNIT_TEST_CHECK(is_file_t(r.get_node(foo_sub_thing)));
  }

  {
    L(FL("delete targets pre-renamed nodes"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_renamed.insert(make_pair(foo_bar, foo));
    cs.nodes_deleted.insert(foo);
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK(r.all_nodes().size() == 2);
    UNIT_TEST_CHECK(is_file_t(r.get_node(foo)));
  }
}

UNIT_TEST(invalid_csets)
{
  temp_node_id_source nis;
  roster_t r;
  MM(r);
  editable_roster_base tree(r, nis);

  file_id f1(decode_hexenc_as<file_id>("0000000000000000000000000000000000000001",
                                       origin::internal));
  file_id f2(decode_hexenc_as<file_id>("0000000000000000000000000000000000000002",
                                       origin::internal));

  file_path root;
  file_path foo = file_path_internal("foo");
  file_path foo_bar = file_path_internal("foo/bar");
  file_path baz = file_path_internal("baz");
  file_path quux = file_path_internal("quux");

  {
    L(FL("TEST: can't double-delete"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(foo_bar);
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't double-add file"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.files_added.insert(make_pair(baz, f2));
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't add file on top of dir"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.files_added.insert(make_pair(foo, f2));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't delete+rename"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(foo_bar);
    cs.nodes_renamed.insert(make_pair(foo_bar, baz));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't add+rename"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.dirs_added.insert(baz);
    cs.nodes_renamed.insert(make_pair(baz, quux));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't add on top of root dir"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.dirs_added.insert(root);
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't rename on top of root dir"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_renamed.insert(make_pair(foo, root));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't rename 'a' 'a'"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_renamed.insert(make_pair(foo_bar, foo_bar));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't rename 'a' 'b'; rename 'a/foo' 'b/foo'"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    file_path baz_bar = file_path_internal("baz/bar");
    cs.nodes_renamed.insert(make_pair(foo, baz));
    cs.nodes_renamed.insert(make_pair(foo_bar, baz_bar));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't attr_set + attr_cleared"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_set.insert(make_pair(make_pair(foo_bar, attr_key("blah")),
                                       attr_value("blahblah")));
    cs.attrs_cleared.insert(make_pair(foo_bar, attr_key("blah")));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't no-op attr_set"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_set.insert(make_pair(make_pair(foo_bar, attr_key("attr_file")),
                                       attr_value("value_file")));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't clear non-existent attr"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_cleared.insert(make_pair(foo_bar, attr_key("blah")));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't clear non-existent attr that once existed"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.attrs_cleared.insert(make_pair(foo_bar, attr_key("attr_file")));
    // exists now, so should be fine
    UNIT_TEST_CHECK_NOT_THROW(cs.apply_to(tree), logic_error);
    // but last time killed it, so can't be killed again
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't have no-op deltas"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.deltas_applied.insert(make_pair(foo_bar,
                                            make_pair(f1, f1)));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't have add+delta"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.files_added.insert(make_pair(baz, f1));
    cs.deltas_applied.insert(make_pair(baz,
                                            make_pair(f1, f2)));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't delta a directory"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.deltas_applied.insert(make_pair(foo,
                                            make_pair(f1, f2)));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't delete non-empty directory"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(foo);
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: attach node with no root directory present"));
    // for this test, make sure original roster has no contents
    r = roster_t();
    cset cs; MM(cs);
    file_path sp = file_path_internal("blah/blah/blah");
    cs.dirs_added.insert(sp);
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
  {
    L(FL("TEST: can't move a directory underneath itself"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    file_path foo_blah = file_path_internal("foo/blah");
    cs.nodes_renamed.insert(make_pair(foo, foo_blah));
    UNIT_TEST_CHECK_THROW(cs.apply_to(tree), logic_error);
  }
}

UNIT_TEST(root_dir)
{
  temp_node_id_source nis;
  roster_t r;
  MM(r);
  editable_roster_base tree(r, nis);

  file_id f1(decode_hexenc_as<file_id>("0000000000000000000000000000000000000001",
                                       origin::internal));

  file_path root, baz = file_path_internal("baz");

  {
    L(FL("TEST: can rename root"));
    setup_roster(r, f1, nis);
    cset cs; MM(cs);
    file_path sp1, sp2;
    cs.dirs_added.insert(root);
    cs.nodes_renamed.insert(make_pair(root, baz));
    cs.apply_to(tree);
    r.check_sane(true);
  }
  {
    L(FL("TEST: can delete root (but it makes us insane)"));
    // for this test, make sure root has no contents
    r = roster_t();
    r.attach_node(r.create_dir_node(nis), root);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(root);
    cs.apply_to(tree);
    UNIT_TEST_CHECK_THROW(r.check_sane(true), logic_error);
  }
  {
    L(FL("TEST: can delete and replace root"));
    r = roster_t();
    r.attach_node(r.create_dir_node(nis), root);
    cset cs; MM(cs);
    cs.nodes_deleted.insert(root);
    cs.dirs_added.insert(root);
    cs.apply_to(tree);
    r.check_sane(true);
  }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
