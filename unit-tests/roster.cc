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
#include "unit_tests.hh"

#include "../roster.cc"

#include "randomizer.hh"
#include "roster_delta.hh"
#include "roster_tests.hh"
#include "constants.hh"

using std::logic_error;
using std::search;
using boost::shared_ptr;


static bool operator==(marking_map const & a, marking_map const & b)
{
  if (a.size() != b.size())
    return false;
  marking_map::const_iterator ai = a.begin();
  marking_map::const_iterator bi = b.begin();
  while (ai != a.end())
    {
      if (ai->first != bi->first)
        return false;
      if (!(*ai->second == *bi->second))
        return false;
      ++ai;
      ++bi;
    }
  return true;
}

static void
make_fake_marking_for(roster_t const & r, marking_map & mm)
{
  mm.clear();
  revision_id rid(decode_hexenc_as<revision_id>("0123456789abcdef0123456789abcdef01234567",
                                                origin::internal));
  for (node_map::const_iterator i = r.all_nodes().begin(); i != r.all_nodes().end();
       ++i)
    {
      mark_new_node(rid, i->second, mm);
    }
}

static void
do_testing_on_one_roster(roster_t const & r)
{
  if (!r.has_root())
    {
      I(r.all_nodes().empty());
      // not much testing to be done on an empty roster -- can't iterate over
      // it or read/write it.
      return;
    }

  MM(r);
  // test dfs_iter by making sure it returns the same number of items as there
  // are items in all_nodes()
  int n; MM(n);
  n = r.all_nodes().size();
  int dfs_counted = 0; MM(dfs_counted);
  for (dfs_iter i(downcast_to_dir_t(r.get_node(file_path())));
       !i.finished(); ++i)
    ++dfs_counted;
  I(n == dfs_counted);

  // Test dfs_iter's path calculations.
  for (dfs_iter i(downcast_to_dir_t(r.get_node(file_path())), true);
       !i.finished(); ++i)
    {
      file_path from_iter = file_path_internal(i.path());
      file_path from_getname;
      const_node_t curr = *i;
      r.get_name(curr->self, from_getname);
      I(from_iter == from_getname);
    }

  // do a read/write spin
  roster_data r_dat; MM(r_dat);
  marking_map fm;
  make_fake_marking_for(r, fm);
  write_roster_and_marking(r, fm, r_dat);
  roster_t r2; MM(r2);
  marking_map fm2;
  read_roster_and_marking(r_dat, r2, fm2);
  I(r == r2);
  I(fm == fm2);
  roster_data r2_dat; MM(r2_dat);
  write_roster_and_marking(r2, fm2, r2_dat);
  I(r_dat == r2_dat);
}

static void
do_testing_on_two_equivalent_csets(cset const & a, cset const & b)
{
  // we do all this reading/writing/comparing of both strings and objects to
  // cross-check the reading, writing, and comparison logic against each
  // other.  (if, say, there is a field in cset that == forgets to check but
  // that write remembers to include, this should catch it).
  MM(a);
  MM(b);
  I(a == b);

  data a_dat, b_dat, a2_dat, b2_dat;
  MM(a_dat);
  MM(b_dat);
  MM(a2_dat);
  MM(b2_dat);

  write_cset(a, a_dat);
  write_cset(b, b_dat);
  I(a_dat == b_dat);
  cset a2, b2;
  MM(a2);
  MM(b2);
  read_cset(a_dat, a2);
  read_cset(b_dat, b2);
  I(a2 == a);
  I(b2 == b);
  I(b2 == a);
  I(a2 == b);
  I(a2 == b2);
  write_cset(a2, a2_dat);
  write_cset(b2, b2_dat);
  I(a_dat == a2_dat);
  I(b_dat == b2_dat);
}

static void
apply_cset_and_do_testing(roster_t & r, cset const & cs, node_id_source & nis)
{
  MM(r);
  MM(cs);
  roster_t original = r;
  MM(original);
  I(original == r);

  editable_roster_base e(r, nis);
  cs.apply_to(e);

  cset derived;
  MM(derived);
  make_cset(original, r, derived);

  do_testing_on_two_equivalent_csets(cs, derived);
  do_testing_on_one_roster(r);
}

static void
spin(roster_t const & from, marking_map const & from_marking,
     roster_t const & to, marking_map const & to_marking)
{
  MM(from);
  MM(from_marking);
  MM(to);
  MM(to_marking);
  roster_delta del;
  MM(del);
  delta_rosters(from, from_marking, to, to_marking, del, 0);

  roster_t tmp(from);
  MM(tmp);
  marking_map tmp_marking(from_marking);
  MM(tmp_marking);
  apply_roster_delta(del, tmp, tmp_marking);
  I(tmp == to);
  I(tmp_marking == to_marking);

  roster_delta del2;
  delta_rosters(from, from_marking, tmp, tmp_marking, del2, 0);
  I(del == del2);
}

void test_roster_delta_on(roster_t const & a, marking_map const & a_marking,
                          roster_t const & b, marking_map const & b_marking)
{
  spin(a, a_marking, b, b_marking);
  spin(b, b_marking, a, a_marking);
}

static void
tests_on_two_rosters(roster_t const & a, roster_t const & b, node_id_source & nis)
{
  MM(a);
  MM(b);

  do_testing_on_one_roster(a);
  do_testing_on_one_roster(b);

  cset a_to_b; MM(a_to_b);
  cset b_to_a; MM(b_to_a);
  make_cset(a, b, a_to_b);
  make_cset(b, a, b_to_a);
  roster_t a2(b); MM(a2);
  roster_t b2(a); MM(b2);
  // we can't use a cset to entirely empty out a roster, so don't bother doing
  // the apply_to tests towards an empty roster
  // (NOTE: if you notice this special case in a time when root dirs can be
  // renamed or deleted, remove it, it will no longer be necessary.
  if (!a.all_nodes().empty())
    {
      editable_roster_base eb(a2, nis);
      b_to_a.apply_to(eb);
    }
  else
    a2 = a;
  if (!b.all_nodes().empty())
    {
      editable_roster_base ea(b2, nis);
      a_to_b.apply_to(ea);
    }
  else
    b2 = b;
  // We'd like to assert that a2 == a and b2 == b, but we can't, because they
  // will have new ids assigned.
  // But they _will_ have the same manifests, assuming things are working
  // correctly.
  manifest_data a_dat; MM(a_dat);
  manifest_data a2_dat; MM(a2_dat);
  manifest_data b_dat; MM(b_dat);
  manifest_data b2_dat; MM(b2_dat);
  if (a.has_root())
    write_manifest_of_roster(a, a_dat);
  if (a2.has_root())
    write_manifest_of_roster(a2, a2_dat);
  if (b.has_root())
    write_manifest_of_roster(b, b_dat);
  if (b2.has_root())
    write_manifest_of_roster(b2, b2_dat);
  I(a_dat == a2_dat);
  I(b_dat == b2_dat);

  cset a2_to_b2; MM(a2_to_b2);
  cset b2_to_a2; MM(b2_to_a2);
  make_cset(a2, b2, a2_to_b2);
  make_cset(b2, a2, b2_to_a2);
  do_testing_on_two_equivalent_csets(a_to_b, a2_to_b2);
  do_testing_on_two_equivalent_csets(b_to_a, b2_to_a2);

  {
    marking_map a_marking;
    make_fake_marking_for(a, a_marking);
    marking_map b_marking;
    make_fake_marking_for(b, b_marking);
    test_roster_delta_on(a, a_marking, b, b_marking);
  }
}

template<typename M>
typename M::const_iterator
random_element(M const & m, randomizer & rng)
{
  size_t i = rng.uniform(m.size());
  typename M::const_iterator j = m.begin();
  while (i > 0)
    {
      I(j != m.end());
      --i;
      ++j;
    }
  return j;
}

string new_word(randomizer & rng)
{
  static string wordchars = "abcdefghijlkmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static unsigned tick = 0;
  string tmp;
  do
    {
      tmp += wordchars[rng.uniform(wordchars.size())];
    }
  while (tmp.size() < 10 && !rng.flip(10));
  return tmp + lexical_cast<string>(tick++);
}

file_id new_ident(randomizer & rng)
{
  static string tab = "0123456789abcdef";
  string tmp;
  tmp.reserve(constants::idlen);
  for (unsigned i = 0; i < constants::idlen; ++i)
    tmp += tab[rng.uniform(tab.size())];
  return decode_hexenc_as<file_id>(tmp, origin::internal);
}

path_component new_component(randomizer & rng)
{
  return path_component(new_word(rng), origin::internal);
}


attr_key pick_attr(attr_map_t const & attrs, randomizer & rng)
{
  return random_element(attrs, rng)->first;
}

bool parent_of(file_path const & p,
               file_path const & c)
{
  bool is_parent = false;

  // the null path is the parent of all paths.
  if (p.depth() == 0)
    is_parent = true;

  else if (p.depth() <= c.depth())
    {
      string const & ci = c.as_internal();
      string const & pi = p.as_internal();

      string::const_iterator c_anchor =
        search(ci.begin(), ci.end(),
               pi.begin(), pi.end());

      is_parent = (c_anchor == ci.begin() && (ci.size() == pi.size()
                                              || *(ci.begin() + pi.size())
                                              == '/'));
    }

  //     L(FL("path '%s' is%s parent of '%s'")
  //       % p
  //       % (is_parent ? "" : " not")
  //       % c);

  return is_parent;
}

void perform_random_action(roster_t & r, node_id_source & nis, randomizer & rng)
{
  cset c;
  I(r.has_root());
  while (c.empty())
    {
      node_t n = random_element(r.all_nodes(), rng)->second;
      file_path pth;
      r.get_name(n->self, pth);
      // L(FL("considering acting on '%s'") % pth);

      switch (rng.uniform(7))
        {
        default:
        case 0:
        case 1:
        case 2:
          if (is_file_t(n) || (pth.depth() > 1 && rng.flip()))
            // Add a sibling of an existing entry.
            pth = pth.dirname() / new_component(rng);

          else
            // Add a child of an existing entry.
            pth = pth / new_component(rng);

          if (rng.flip())
            {
              // L(FL("adding dir '%s'") % pth);
              safe_insert(c.dirs_added, pth);
            }
          else
            {
              // L(FL("adding file '%s'") % pth);
              safe_insert(c.files_added, make_pair(pth, new_ident(rng)));
            }
          break;

        case 3:
          if (is_file_t(n))
            {
              // L(FL("altering content of file '%s'") % pth);
              safe_insert(c.deltas_applied,
                          make_pair(pth,
                                    make_pair(downcast_to_file_t(n)->content,
                                              new_ident(rng))));
            }
          break;

        case 4:
          {
            node_t n2 = random_element(r.all_nodes(), rng)->second;
            if (n == n2)
              continue;

            file_path pth2;
            r.get_name(n2->self, pth2);

            if (is_file_t(n2) || (pth2.depth() > 1 && rng.flip()))
              {
                // L(FL("renaming to a sibling of an existing entry '%s'")
                //   % pth2);
                // Move to a sibling of an existing entry.
                pth2 = pth2.dirname() / new_component(rng);
              }

            else
              {
                // L(FL("renaming to a child of an existing entry '%s'")
                //   % pth2);
                // Move to a child of an existing entry.
                pth2 = pth2 / new_component(rng);
              }

            if (!parent_of(pth, pth2))
              {
                // L(FL("renaming '%s' -> '%s") % pth % pth2);
                safe_insert(c.nodes_renamed, make_pair(pth, pth2));
              }
          }
          break;

        case 5:
          if (!null_node(n->parent)
              && (is_file_t(n) || downcast_to_dir_t(n)->children.empty())
              && r.all_nodes().size() > 1) // do not delete the root
            {
              // L(FL("deleting '%s'") % pth);
              safe_insert(c.nodes_deleted, pth);
            }
          break;

        case 6:
          if (!n->attrs.empty() && rng.flip())
            {
              attr_key k = pick_attr(n->attrs, rng);
              if (safe_get(n->attrs, k).first)
                {
                  if (rng.flip())
                    {
                      // L(FL("clearing attr on '%s'") % pth);
                      safe_insert(c.attrs_cleared, make_pair(pth, k));
                    }
                  else
                    {
                      // L(FL("changing attr on '%s'\n") % pth);
                      safe_insert(c.attrs_set,
                                  make_pair(make_pair(pth, k),
                                            attr_value(new_word(rng), origin::internal)));
                    }
                }
              else
                {
                  // L(FL("setting previously set attr on '%s'") % pth);
                  safe_insert(c.attrs_set,
                              make_pair(make_pair(pth, k),
                                        attr_value(new_word(rng), origin::internal)));
                }
            }
          else
            {
              // L(FL("setting attr on '%s'") % pth);
              safe_insert(c.attrs_set,
                          make_pair(make_pair(pth, attr_key(new_word(rng), origin::internal)),
                                    attr_value(new_word(rng), origin::internal)));
            }
          break;
        }
    }
  // now do it
  apply_cset_and_do_testing(r, c, nis);
}

const node_id first_node = 1;
testing_node_id_source::testing_node_id_source()
  : curr(first_node)
{}

node_id
testing_node_id_source::next()
{
  // L(FL("creating node %x") % curr);
  node_id n = curr++;
  I(!temp_node(n));
  return n;
}

template <> void
dump(int const & i, string & out)
{
  out = lexical_cast<string>(i) + "\n";
}

UNIT_TEST(random_actions)
{
  randomizer rng;
  roster_t r;
  testing_node_id_source nis;

  roster_t empty, prev, recent, ancient;

  {
    // give all the rosters a root
    cset c;
    c.dirs_added.insert(file_path());
    apply_cset_and_do_testing(r, c, nis);
  }

  empty = ancient = recent = prev = r;
  for (int i = 0; i < 2000; )
    {
      int manychanges = 100 + rng.uniform(300);
      // P(F("random roster actions: outer step at %d, making %d changes")
      //   % i % manychanges);

      for (int outer_limit = i + manychanges; i < outer_limit; )
        {
          int fewchanges = 5 + rng.uniform(10);
          // P(F("random roster actions: inner step at %d, making %d changes")
          //   % i % fewchanges);

          for (int inner_limit = i + fewchanges; i < inner_limit; i++)
            {
              // P(F("random roster actions: change %d") % i);
              perform_random_action(r, nis, rng);
              I(!(prev == r));
              prev = r;
            }
          tests_on_two_rosters(recent, r, nis);
          tests_on_two_rosters(empty, r, nis);
          recent = r;
        }
      tests_on_two_rosters(ancient, r, nis);
      ancient = r;
    }
}

// some of our raising operations leave our state corrupted.  so rather than
// trying to do all the illegal things in one pass, we re-run this function a
// bunch of times, and each time we do only one of these potentially
// corrupting tests.  Test numbers are in the range [0, total).

#define MAYBE(code) if (total == to_run) { L(FL(#code)); code; return; } ++total

static void
check_sane_roster_do_tests(int to_run, int& total)
{
  total = 0;
  testing_node_id_source nis;
  roster_t r;
  MM(r);

  // roster must have a root dir
  MAYBE(UNIT_TEST_CHECK_THROW(r.check_sane(false), logic_error));
  MAYBE(UNIT_TEST_CHECK_THROW(r.check_sane(true), logic_error));

  file_path fp_;
  file_path fp_foo = file_path_internal("foo");
  file_path fp_foo_bar = file_path_internal("foo/bar");
  file_path fp_foo_baz = file_path_internal("foo/baz");

  node_id nid_f = r.create_file_node(decode_hexenc_as<file_id>("0000000000000000000000000000000000000000",
                                                               origin::internal),
                                     nis);
  // root must be a directory, not a file
  MAYBE(UNIT_TEST_CHECK_THROW(r.attach_node(nid_f, fp_), logic_error));

  node_id root_dir = r.create_dir_node(nis);
  r.attach_node(root_dir, fp_);
  // has a root dir, but a detached file
  MAYBE(UNIT_TEST_CHECK_THROW(r.check_sane(false), logic_error));
  MAYBE(UNIT_TEST_CHECK_THROW(r.check_sane(true), logic_error));

  r.attach_node(nid_f, fp_foo);
  // now should be sane
  UNIT_TEST_CHECK_NOT_THROW(r.check_sane(false), logic_error);
  UNIT_TEST_CHECK_NOT_THROW(r.check_sane(true), logic_error);

  node_id nid_d = r.create_dir_node(nis);
  // if "foo" exists, can't attach another node at "foo"
  MAYBE(UNIT_TEST_CHECK_THROW(r.attach_node(nid_d, fp_foo), logic_error));
  // if "foo" is a file, can't attach a node at "foo/bar"
  MAYBE(UNIT_TEST_CHECK_THROW(r.attach_node(nid_d, fp_foo_bar), logic_error));

  UNIT_TEST_CHECK(r.detach_node(fp_foo) == nid_f);
  r.attach_node(nid_d, fp_foo);
  r.attach_node(nid_f, fp_foo_bar);
  UNIT_TEST_CHECK_NOT_THROW(r.check_sane(false), logic_error);
  UNIT_TEST_CHECK_NOT_THROW(r.check_sane(true), logic_error);

  temp_node_id_source nis_tmp;
  node_id nid_tmp = r.create_dir_node(nis_tmp);
  // has a detached node
  MAYBE(UNIT_TEST_CHECK_THROW(r.check_sane(false), logic_error));
  MAYBE(UNIT_TEST_CHECK_THROW(r.check_sane(true), logic_error));
  r.attach_node(nid_tmp, fp_foo_baz);
  // now has no detached nodes, but one temp node
  MAYBE(UNIT_TEST_CHECK_THROW(r.check_sane(false), logic_error));
  UNIT_TEST_CHECK_NOT_THROW(r.check_sane(true), logic_error);
}

#undef MAYBE

UNIT_TEST(check_sane_roster)
{
  int total;
  check_sane_roster_do_tests(-1, total);
  for (int to_run = 0; to_run < total; ++to_run)
    {
      L(FL("check_sane_roster_test: loop = %i (of %i)") % to_run % (total - 1));
      int tmp;
      check_sane_roster_do_tests(to_run, tmp);
    }
}

UNIT_TEST(check_sane_roster_loop)
{
  testing_node_id_source nis;
  roster_t r; MM(r);
  file_path root;
  r.attach_node(r.create_dir_node(nis), root);
  node_id nid_foo = r.create_dir_node(nis);
  node_id nid_bar = r.create_dir_node(nis);
  r.attach_node(nid_foo, nid_bar, path_component("foo"));
  r.attach_node(nid_bar, nid_foo, path_component("bar"));
  UNIT_TEST_CHECK_THROW(r.check_sane(true), logic_error);
}

UNIT_TEST(check_sane_roster_screwy_dir_map)
{
  testing_node_id_source nis;
  roster_t r; MM(r);
  file_path root;
  r.attach_node(r.create_dir_node(nis), root);
  roster_t other; MM(other);
  node_id other_nid = other.create_dir_node(nis);
  dir_t root_n = downcast_to_dir_t(r.get_node_for_update(root));
  root_n->children.insert(make_pair(path_component("foo"),
                                    other.get_node_for_update(other_nid)));
  UNIT_TEST_CHECK_THROW(r.check_sane(), logic_error);
  // well, but that one was easy, actually, because a dir traversal will hit
  // more nodes than actually exist... so let's make it harder, by making sure
  // that a dir traversal will hit exactly as many nodes as actually exist.
  node_id distractor_nid = r.create_dir_node(nis);
  UNIT_TEST_CHECK_THROW(r.check_sane(), logic_error);
  // and even harder, by making that node superficially valid too
  dir_t distractor_n = downcast_to_dir_t(r.get_node_for_update(distractor_nid));
  distractor_n->parent = distractor_nid;
  distractor_n->name = path_component("foo");
  distractor_n->children.insert(make_pair(distractor_n->name, distractor_n));
  UNIT_TEST_CHECK_THROW(r.check_sane(), logic_error);
}

UNIT_TEST(bad_attr)
{
  testing_node_id_source nis;
  roster_t r; MM(r);
  file_path root;
  r.attach_node(r.create_dir_node(nis), root);
  UNIT_TEST_CHECK_THROW(r.set_attr(root, attr_key("test_key1"),
                               make_pair(false, attr_value("invalid"))),
                    logic_error);
  UNIT_TEST_CHECK_NOT_THROW(r.check_sane(true), logic_error);
  safe_insert(r.get_node_for_update(root)->attrs,
              make_pair(attr_key("test_key2"),
                        make_pair(false, attr_value("invalid"))));
  UNIT_TEST_CHECK_THROW(r.check_sane(true), logic_error);
}

////////////////////////////////////////////////////////////////////////
// exhaustive marking tests
////////////////////////////////////////////////////////////////////////

// The marking/roster generation code is extremely critical.  It is the very
// core of monotone's versioning technology, very complex, and bugs can result
// in corrupt and nonsensical histories (not to mention erroneous merges and
// the like).  Furthermore, the code that implements it is littered with
// case-by-case analysis, where copy-paste errors could easily occur.  So the
// purpose of this section is to systematically and exhaustively test every
// possible case.
//
// Our underlying merger, *-merge, works on scalars, case-by-case.
// The cases are:
//   0 parent:
//       a*
//   1 parent:
//       a     a
//       |     |
//       a     b*
//   2 parents:
//       a   a  a   a  a   b  a   b
//        \ /    \ /    \ /    \ /
//         a      b*     c*     a?
//
// Each node has a number of scalars associated with it:
//   * basename+parent
//   * file content (iff a file)
//   * attributes
//
// So for each scalar, we want to test each way it can appear in each of the
// above shapes.  This is made more complex by lifecycles.  We can achieve a 0
// parent node as:
//   * a node in a 0-parent roster (root revision)
//   * a newly added node in a 1-parent roster
//   * a newly added node in a 2-parent roster
// a 1 parent node as:
//   * a pre-existing node in a 1-parent roster
//   * a node in a 2-parent roster that only existed in one of the parents
// a 2 parent node as:
//   * a pre-existing node in a 2-parent roster
//
// Because the basename+parent and file_content scalars have lifetimes that
// exactly match the lifetime of the node they are on, those are all the cases
// for these scalars.  However, attrs make things a bit more complicated,
// because they can be added.  An attr can have 0 parents:
//   * in any of the above cases, with an attribute newly added on the node
// And one parent:
//   * in any of the cases above with one node parent and the attr pre-existing
//   * in a 2-parent node where the attr exists in only one of the parents
//
// Plus, just to be sure, in the merge cases we check both the given example
// and the mirror-reversed one, since the code implementing this could
// conceivably mark merge(A, B) right but get merge(B, A) wrong.  And for the
// scalars that can appear on either files or dirs, we check both.

// The following somewhat elaborate code implements all these checks.  The
// most important background assumption to know, is that it always assumes
// (and this assumption is hard-coded in various places) that it is looking at
// one of the following topologies:
//
//     old
//
//     old
//      |
//     new
//
//     old
//     / \.
// left   right
//     \ /
//     new
//
// There is various tricksiness in making sure that the root directory always
// has the right birth_revision, that nodes are created with good birth
// revisions and sane markings on the scalars we are not interested in, etc.
// This code is ugly and messy and could use refactoring, but it seems to
// work.

////////////////
// These are some basic utility pieces handy for the exhaustive mark tests

namespace
{
  template <typename T> set<T>
  singleton(T const & t)
  {
    set<T> s;
    s.insert(t);
    return s;
  }

  template <typename T> set<T>
  doubleton(T const & t1, T const & t2)
  {
    set<T> s;
    s.insert(t1);
    s.insert(t2);
    return s;
  }

  revision_id old_rid(string(constants::idlen_bytes, '\x00'), origin::internal);
  revision_id left_rid(string(constants::idlen_bytes, '\x11'), origin::internal);
  revision_id right_rid(string(constants::idlen_bytes, '\x22'), origin::internal);
  revision_id new_rid(string(constants::idlen_bytes, '\x44'), origin::internal);

////////////////
// These classes encapsulate information about all the different scalars
// that *-merge applies to.

  typedef enum { scalar_a, scalar_b, scalar_c,
                 scalar_none, scalar_none_2 } scalar_val;

  void
  dump(scalar_val const & val, string & out)
  {
    switch (val)
      {
      case scalar_a: out = "scalar_a"; break;
      case scalar_b: out = "scalar_b"; break;
      case scalar_c: out = "scalar_c"; break;
      case scalar_none: out = "scalar_none"; break;
      case scalar_none_2: out = "scalar_none_2"; break;
      }
    out += "\n";
  }

  struct a_scalar
  {
    // Must use std::set in arguments to avoid "changes meaning" errors.
    virtual void set(revision_id const & scalar_origin_rid,
                     scalar_val val,
                     std::set<revision_id> const & this_scalar_mark,
                     roster_t & roster, marking_map & markings)
      = 0;
    virtual ~a_scalar() {};

    node_id_source & nis;
    node_id const root_nid;
    node_id const obj_under_test_nid;
    a_scalar(node_id_source & nis)
      : nis(nis), root_nid(nis.next()), obj_under_test_nid(nis.next())
    {}

    void setup(roster_t & roster, marking_map & markings)
    {
      roster.create_dir_node(root_nid);
      roster.attach_node(root_nid, file_path_internal(""));
      marking_t m(new marking());
      m->birth_revision = old_rid;
      m->parent_name.insert(old_rid);
      markings.put_marking(root_nid, m);
    }

    virtual string my_type() const = 0;

    virtual void dump(string & out) const
    {
      ostringstream oss;
      oss << "type: " << my_type() << '\n'
          << "root_nid: " << root_nid << '\n'
          << "obj_under_test_nid: " << obj_under_test_nid << '\n';
      out = oss.str();
    }
  };

  void
  dump(a_scalar const & s, string & out)
  {
    s.dump(out);
  }

  struct file_maker
  {
    static void make_obj(revision_id const & scalar_origin_rid, node_id nid,
                         roster_t & roster, marking_map & markings)
    {
      make_file(scalar_origin_rid, nid,
                file_id(string(constants::idlen_bytes, '\xaa'), origin::internal),
                roster, markings);
    }
    static void make_file(revision_id const & scalar_origin_rid, node_id nid,
                          file_id const & fid,
                          roster_t & roster, marking_map & markings)
    {
      roster.create_file_node(fid, nid);
      marking_t m(new marking());
      m->birth_revision = scalar_origin_rid;
      m->parent_name = m->file_content = singleton(scalar_origin_rid);
      markings.put_marking(nid, m);
    }
  };

  struct dir_maker
  {
    static void make_obj(revision_id const & scalar_origin_rid, node_id nid,
                         roster_t & roster, marking_map & markings)
    {
      roster.create_dir_node(nid);
      marking_t m(new marking());
      m->birth_revision = scalar_origin_rid;
      m->parent_name = singleton(scalar_origin_rid);
      markings.put_marking(nid, m);
    }
  };

  struct file_content_scalar : public a_scalar
  {
    virtual string my_type() const { return "file_content_scalar"; }

    map<scalar_val, file_id> values;
    file_content_scalar(node_id_source & nis)
      : a_scalar(nis)
    {
      safe_insert(values,
                  make_pair(scalar_a,
                            file_id(string(constants::idlen_bytes, '\xaa'),
                                    origin::internal)));
      safe_insert(values,
                  make_pair(scalar_b,
                            file_id(string(constants::idlen_bytes, '\xbb'),
                                    origin::internal)));
      safe_insert(values,
                  make_pair(scalar_c,
                            file_id(string(constants::idlen_bytes, '\xcc'),
                                    origin::internal)));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      if (val != scalar_none)
        {
          file_maker::make_file(scalar_origin_rid, obj_under_test_nid,
                                safe_get(values, val),
                                roster, markings);
          roster.attach_node(obj_under_test_nid, file_path_internal("foo"));
          markings.get_marking_for_update(obj_under_test_nid)->file_content = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };

  template <typename T>
  struct X_basename_scalar : public a_scalar
  {
    virtual string my_type() const { return "X_basename_scalar"; }

    map<scalar_val, file_path> values;
    X_basename_scalar(node_id_source & nis)
      : a_scalar(nis)
    {
      safe_insert(values, make_pair(scalar_a, file_path_internal("a")));
      safe_insert(values, make_pair(scalar_b, file_path_internal("b")));
      safe_insert(values, make_pair(scalar_c, file_path_internal("c")));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      if (val != scalar_none)
        {
          T::make_obj(scalar_origin_rid, obj_under_test_nid, roster, markings);
          roster.attach_node(obj_under_test_nid, safe_get(values, val));
          markings.get_marking_for_update(obj_under_test_nid)->parent_name = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };

  template <typename T>
  struct X_parent_scalar : public a_scalar
  {
    virtual string my_type() const { return "X_parent_scalar"; }

    map<scalar_val, file_path> values;
    node_id const a_nid, b_nid, c_nid;
    X_parent_scalar(node_id_source & nis)
      : a_scalar(nis), a_nid(nis.next()), b_nid(nis.next()), c_nid(nis.next())
    {
      safe_insert(values, make_pair(scalar_a, file_path_internal("dir_a/foo")));
      safe_insert(values, make_pair(scalar_b, file_path_internal("dir_b/foo")));
      safe_insert(values, make_pair(scalar_c, file_path_internal("dir_c/foo")));
    }
    void
    setup_dirs(roster_t & roster, marking_map & markings)
    {
      roster.create_dir_node(a_nid);
      roster.attach_node(a_nid, file_path_internal("dir_a"));
      roster.create_dir_node(b_nid);
      roster.attach_node(b_nid, file_path_internal("dir_b"));
      roster.create_dir_node(c_nid);
      roster.attach_node(c_nid, file_path_internal("dir_c"));
      marking_t m(new marking());
      m->birth_revision = old_rid;
      m->parent_name.insert(old_rid);
      markings.put_marking(a_nid, m);
      markings.put_marking(b_nid, m);
      markings.put_marking(c_nid, m);
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      setup_dirs(roster, markings);
      if (val != scalar_none)
        {
          T::make_obj(scalar_origin_rid, obj_under_test_nid, roster, markings);
          roster.attach_node(obj_under_test_nid, safe_get(values, val));
          markings.get_marking_for_update(obj_under_test_nid)->parent_name = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };

  // this scalar represents an attr whose node already exists, and we put an
  // attr on it.
  template <typename T>
  struct X_attr_existing_node_scalar : public a_scalar
  {
    virtual string my_type() const { return "X_attr_scalar"; }

    map<scalar_val, pair<bool, attr_value> > values;
    X_attr_existing_node_scalar(node_id_source & nis)
      : a_scalar(nis)
    {
      safe_insert(values, make_pair(scalar_a, make_pair(true, attr_value("a"))));
      safe_insert(values, make_pair(scalar_b, make_pair(true, attr_value("b"))));
      safe_insert(values, make_pair(scalar_c, make_pair(true, attr_value("c"))));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      // _not_ scalar_origin_rid, because our object exists everywhere, regardless of
      // when the attr shows up
      T::make_obj(old_rid, obj_under_test_nid, roster, markings);
      roster.attach_node(obj_under_test_nid, file_path_internal("foo"));
      if (val != scalar_none)
        {
          safe_insert(roster.get_node_for_update(obj_under_test_nid)->attrs,
                      make_pair(attr_key("test_key"), safe_get(values, val)));
          markings.get_marking_for_update(obj_under_test_nid)->attrs[attr_key("test_key")] = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };

  // this scalar represents an attr whose node does not exist; we create the
  // node when we create the attr.
  template <typename T>
  struct X_attr_new_node_scalar : public a_scalar
  {
    virtual string my_type() const { return "X_attr_scalar"; }

    map<scalar_val, pair<bool, attr_value> > values;
    X_attr_new_node_scalar(node_id_source & nis)
      : a_scalar(nis)
    {
      safe_insert(values, make_pair(scalar_a, make_pair(true, attr_value("a"))));
      safe_insert(values, make_pair(scalar_b, make_pair(true, attr_value("b"))));
      safe_insert(values, make_pair(scalar_c, make_pair(true, attr_value("c"))));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      if (val != scalar_none)
        {
          T::make_obj(scalar_origin_rid, obj_under_test_nid, roster, markings);
          roster.attach_node(obj_under_test_nid, file_path_internal("foo"));
          safe_insert(roster.get_node_for_update(obj_under_test_nid)->attrs,
                      make_pair(attr_key("test_key"), safe_get(values, val)));
          markings.get_marking_for_update(obj_under_test_nid)->attrs[attr_key("test_key")] = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };

  typedef vector<shared_ptr<a_scalar> > scalars;
  scalars
  all_scalars(node_id_source & nis)
  {
    scalars ss;
    ss.push_back(shared_ptr<a_scalar>(new file_content_scalar(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_basename_scalar<file_maker>(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_basename_scalar<dir_maker>(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_parent_scalar<file_maker>(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_parent_scalar<dir_maker>(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_attr_existing_node_scalar<file_maker>(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_attr_existing_node_scalar<dir_maker>(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_attr_new_node_scalar<file_maker>(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_attr_new_node_scalar<dir_maker>(nis)));
    return ss;
  }
}

////////////////
// These functions encapsulate the logic for running a particular mark
// scenario with a particular scalar with 0, 1, or 2 roster parents.

static void
run_with_0_roster_parents(a_scalar & s, revision_id scalar_origin_rid,
                          scalar_val new_val,
                          set<revision_id> const & new_mark_set,
                          node_id_source & nis)
{
  MM(s);
  MM(scalar_origin_rid);
  MM(new_val);
  MM(new_mark_set);
  roster_t expected_roster; MM(expected_roster);
  marking_map expected_markings; MM(expected_markings);

  s.set(scalar_origin_rid, new_val, new_mark_set, expected_roster, expected_markings);

  roster_t empty_roster;
  cset cs; MM(cs);
  make_cset(empty_roster, expected_roster, cs);

  roster_t new_roster; MM(new_roster);
  marking_map new_markings; MM(new_markings);
  // this function takes the old parent roster/marking and modifies them; in
  // our case, the parent roster/marking are empty, and so are our
  // roster/marking, so we don't need to do anything special.
  make_roster_for_nonmerge(cs, old_rid, new_roster, new_markings, nis);

  I(equal_up_to_renumbering(expected_roster, expected_markings,
                            new_roster, new_markings));

  marking_map new_markings2; MM(new_markings2);
  mark_roster_with_no_parents(old_rid, new_roster, new_markings2);
  I(new_markings == new_markings2);

  marking_map new_markings3; MM(new_markings3);
  roster_t parent3;
  marking_map old_markings3;
  mark_roster_with_one_parent(parent3, old_markings3, old_rid, new_roster,
                              new_markings3);
  I(new_markings == new_markings3);
}

static void
run_with_1_roster_parent(a_scalar & s,
                         revision_id scalar_origin_rid,
                         scalar_val parent_val,
                         set<revision_id> const & parent_mark_set,
                         scalar_val new_val,
                         set<revision_id> const & new_mark_set,
                         node_id_source & nis)
{
  MM(s);
  MM(scalar_origin_rid);
  MM(parent_val);
  MM(parent_mark_set);
  MM(new_val);
  MM(new_mark_set);
  roster_t parent_roster; MM(parent_roster);
  marking_map parent_markings; MM(parent_markings);
  roster_t expected_roster; MM(expected_roster);
  marking_map expected_markings; MM(expected_markings);

  s.set(scalar_origin_rid, parent_val, parent_mark_set, parent_roster, parent_markings);
  s.set(scalar_origin_rid, new_val, new_mark_set, expected_roster, expected_markings);

  cset cs; MM(cs);
  make_cset(parent_roster, expected_roster, cs);

  roster_t new_roster; MM(new_roster);
  marking_map new_markings; MM(new_markings);
  new_roster = parent_roster;
  new_markings = parent_markings;
  make_roster_for_nonmerge(cs, new_rid, new_roster, new_markings, nis);

  I(equal_up_to_renumbering(expected_roster, expected_markings,
                            new_roster, new_markings));

  marking_map new_markings2; MM(new_markings2);
  mark_roster_with_one_parent(parent_roster, parent_markings,
                              new_rid, new_roster, new_markings2);
  I(new_markings == new_markings2);
}

static void
run_with_2_roster_parents(a_scalar & s,
                          revision_id scalar_origin_rid,
                          scalar_val left_val,
                          set<revision_id> const & left_mark_set,
                          scalar_val right_val,
                          set<revision_id> const & right_mark_set,
                          scalar_val new_val,
                          set<revision_id> const & new_mark_set,
                          node_id_source & nis)
{
  MM(s);
  MM(scalar_origin_rid);
  MM(left_val);
  MM(left_mark_set);
  MM(right_val);
  MM(right_mark_set);
  MM(new_val);
  MM(new_mark_set);
  roster_t left_roster; MM(left_roster);
  roster_t right_roster; MM(right_roster);
  roster_t expected_roster; MM(expected_roster);
  marking_map left_markings; MM(left_markings);
  marking_map right_markings; MM(right_markings);
  marking_map expected_markings; MM(expected_markings);

  s.set(scalar_origin_rid, left_val, left_mark_set, left_roster, left_markings);
  s.set(scalar_origin_rid, right_val, right_mark_set, right_roster, right_markings);
  s.set(scalar_origin_rid, new_val, new_mark_set, expected_roster, expected_markings);

  cset left_cs; MM(left_cs);
  cset right_cs; MM(right_cs);
  make_cset(left_roster, expected_roster, left_cs);
  make_cset(right_roster, expected_roster, right_cs);

  set<revision_id> left_uncommon_ancestors; MM(left_uncommon_ancestors);
  left_uncommon_ancestors.insert(left_rid);
  set<revision_id> right_uncommon_ancestors; MM(right_uncommon_ancestors);
  right_uncommon_ancestors.insert(right_rid);

  roster_t new_roster; MM(new_roster);
  marking_map new_markings; MM(new_markings);
  make_roster_for_merge(left_rid, left_roster, left_markings, left_cs,
                        left_uncommon_ancestors,
                        right_rid, right_roster, right_markings, right_cs,
                        right_uncommon_ancestors,
                        new_rid, new_roster, new_markings,
                        nis);

  I(equal_up_to_renumbering(expected_roster, expected_markings,
                            new_roster, new_markings));
}

////////////////
// These functions encapsulate all the different ways to get a 0 parent node,
// a 1 parent node, and a 2 parent node.

////////////////
// These functions encapsulate all the different ways to get a 0 parent
// scalar, a 1 parent scalar, and a 2 parent scalar.

// FIXME: have clients just use s.nis instead of passing it separately...?

static void
run_a_2_scalar_parent_mark_scenario_exact(revision_id const & scalar_origin_rid,
                                          scalar_val left_val,
                                          set<revision_id> const & left_mark_set,
                                          scalar_val right_val,
                                          set<revision_id> const & right_mark_set,
                                          scalar_val new_val,
                                          set<revision_id> const & new_mark_set)
{
  testing_node_id_source nis;
  scalars ss = all_scalars(nis);
  for (scalars::const_iterator i = ss.begin(); i != ss.end(); ++i)
    {
      run_with_2_roster_parents(**i, scalar_origin_rid,
                                left_val, left_mark_set,
                                right_val, right_mark_set,
                                new_val, new_mark_set,
                                nis);
    }
}

static revision_id
flip_revision_id(revision_id const & rid)
{
  if (rid == old_rid || rid == new_rid)
    return rid;
  else if (rid == left_rid)
    return right_rid;
  else if (rid == right_rid)
    return left_rid;
  else
    I(false);
}

static set<revision_id>
flip_revision(set<revision_id> const & rids)
{
  set<revision_id> flipped_rids;
  for (set<revision_id>::const_iterator i = rids.begin(); i != rids.end(); ++i)
    flipped_rids.insert(flip_revision_id(*i));
  return flipped_rids;
}

static void
run_a_2_scalar_parent_mark_scenario(revision_id const & scalar_origin_rid,
                                    scalar_val left_val,
                                    set<revision_id> const & left_mark_set,
                                    scalar_val right_val,
                                    set<revision_id> const & right_mark_set,
                                    scalar_val new_val,
                                    set<revision_id> const & new_mark_set)
{
  // run both what we're given...
  run_a_2_scalar_parent_mark_scenario_exact(scalar_origin_rid,
                                            left_val, left_mark_set,
                                            right_val, right_mark_set,
                                            new_val, new_mark_set);
  // ...and its symmetric reflection.  but we have to flip the mark set,
  // because the exact stuff has hard-coded the names of the various
  // revisions and their uncommon ancestor sets.
  {
    set<revision_id> flipped_left_mark_set = flip_revision(left_mark_set);
    set<revision_id> flipped_right_mark_set = flip_revision(right_mark_set);
    set<revision_id> flipped_new_mark_set = flip_revision(new_mark_set);

    run_a_2_scalar_parent_mark_scenario_exact(flip_revision_id(scalar_origin_rid),
                                              right_val, flipped_right_mark_set,
                                              left_val, flipped_left_mark_set,
                                              new_val, flipped_new_mark_set);
  }
}

static void
run_a_2_scalar_parent_mark_scenario(scalar_val left_val,
                                    set<revision_id> const & left_mark_set,
                                    scalar_val right_val,
                                    set<revision_id> const & right_mark_set,
                                    scalar_val new_val,
                                    set<revision_id> const & new_mark_set)
{
  run_a_2_scalar_parent_mark_scenario(old_rid,
                                      left_val, left_mark_set,
                                      right_val, right_mark_set,
                                      new_val, new_mark_set);
}

static void
run_a_1_scalar_parent_mark_scenario(scalar_val parent_val,
                                    set<revision_id> const & parent_mark_set,
                                    scalar_val new_val,
                                    set<revision_id> const & new_mark_set)
{
  {
    testing_node_id_source nis;
    scalars ss = all_scalars(nis);
    for (scalars::const_iterator i = ss.begin(); i != ss.end(); ++i)
      run_with_1_roster_parent(**i, old_rid,
                               parent_val, parent_mark_set,
                               new_val, new_mark_set,
                               nis);
  }
  // this is an asymmetric, test, so run it via the code that will test it
  // both ways
  run_a_2_scalar_parent_mark_scenario(left_rid,
                                      parent_val, parent_mark_set,
                                      scalar_none, set<revision_id>(),
                                      new_val, new_mark_set);
}

static void
run_a_0_scalar_parent_mark_scenario()
{
  {
    testing_node_id_source nis;
    scalars ss = all_scalars(nis);
    for (scalars::const_iterator i = ss.begin(); i != ss.end(); ++i)
      {
        run_with_0_roster_parents(**i, old_rid, scalar_a, singleton(old_rid), nis);
        run_with_1_roster_parent(**i, new_rid,
                                 scalar_none, set<revision_id>(),
                                 scalar_a, singleton(new_rid),
                                 nis);
        run_with_2_roster_parents(**i, new_rid,
                                  scalar_none, set<revision_id>(),
                                  scalar_none, set<revision_id>(),
                                  scalar_a, singleton(new_rid),
                                  nis);
      }
  }
}

////////////////
// These functions contain the actual list of *-merge cases that we would like
// to test.

UNIT_TEST(all_0_scalar_parent_mark_scenarios)
{
  L(FL("TEST: begin checking 0-parent marking"));
  // a*
  run_a_0_scalar_parent_mark_scenario();
  L(FL("TEST: end checking 0-parent marking"));
}

UNIT_TEST(all_1_scalar_parent_mark_scenarios)
{
  L(FL("TEST: begin checking 1-parent marking"));
  //  a
  //  |
  //  a
  run_a_1_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_a, singleton(old_rid));
  //  a*
  //  |
  //  a
  run_a_1_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_a, singleton(left_rid));
  // a*  a*
  //  \ /
  //   a
  //   |
  //   a
  run_a_1_scalar_parent_mark_scenario(scalar_a, doubleton(left_rid, right_rid),
                                      scalar_a, doubleton(left_rid, right_rid));
  //  a
  //  |
  //  b*
  run_a_1_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_b, singleton(new_rid));
  //  a*
  //  |
  //  b*
  run_a_1_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_b, singleton(new_rid));
  // a*  a*
  //  \ /
  //   a
  //   |
  //   b*
  run_a_1_scalar_parent_mark_scenario(scalar_a, doubleton(left_rid, right_rid),
                                      scalar_b, singleton(new_rid));
  L(FL("TEST: end checking 1-parent marking"));
}

UNIT_TEST(all_2_scalar_parent_mark_scenarios)
{
  L(FL("TEST: begin checking 2-parent marking"));
  ///////////////////////////////////////////////////////////////////
  // a   a
  //  \ /
  //   a
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_a, singleton(old_rid),
                                      scalar_a, singleton(old_rid));
  // a   a*
  //  \ /
  //   a
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_a, singleton(right_rid),
                                      scalar_a, doubleton(old_rid, right_rid));
  // a*  a*
  //  \ /
  //   a
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_a, singleton(right_rid),
                                      scalar_a, doubleton(left_rid, right_rid));

  ///////////////////////////////////////////////////////////////////
  // a   a
  //  \ /
  //   b*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_a, singleton(old_rid),
                                      scalar_b, singleton(new_rid));
  // a   a*
  //  \ /
  //   b*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_a, singleton(right_rid),
                                      scalar_b, singleton(new_rid));
  // a*  a*
  //  \ /
  //   b*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_a, singleton(right_rid),
                                      scalar_b, singleton(new_rid));

  ///////////////////////////////////////////////////////////////////
  //  a*  b*
  //   \ /
  //    c*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_b, singleton(right_rid),
                                      scalar_c, singleton(new_rid));
  //  a   b*
  //   \ /
  //    c*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_b, singleton(right_rid),
                                      scalar_c, singleton(new_rid));
  // this case cannot actually arise, because if *(a) = *(b) then val(a) =
  // val(b).  but hey.
  //  a   b
  //   \ /
  //    c*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_b, singleton(old_rid),
                                      scalar_c, singleton(new_rid));

  ///////////////////////////////////////////////////////////////////
  //  a*  b*
  //   \ /
  //    a*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_b, singleton(right_rid),
                                      scalar_a, singleton(new_rid));
  //  a   b*
  //   \ /
  //    a*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_b, singleton(right_rid),
                                      scalar_a, singleton(new_rid));
  //  a*  b
  //   \ /
  //    a
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_b, singleton(old_rid),
                                      scalar_a, singleton(left_rid));

  // FIXME: be nice to test:
  //  a*  a*  b
  //   \ /   /
  //    a   /
  //     \ /
  //      a
  L(FL("TEST: end checking 2-parent marking"));
}

// there is _one_ remaining case that the above tests miss, because they
// couple scalar lifetimes and node lifetimes.  Maybe they shouldn't do that,
// but anyway... until someone decides to refactor, we need this.  The basic
// issue is that for content and name scalars, the scalar lifetime and the
// node lifetime are identical.  For attrs, this isn't necessarily true.  This
// is why we have two different attr scalars.  Let's say that "." means a node
// that doesn't exist, and "+" means a node that exists but has no roster.
// The first scalar checks cases like
//     +
//     |
//     a
//
//   +   +
//    \ /
//     a*
//
//   a*  +
//    \ /
//     a
// and the second one checks cases like
//     .
//     |
//     a
//
//   .   .
//    \ /
//     a*
//
//   a*  .
//    \ /
//     a
// Between them, they cover _almost_ all possibilities.  The one that they
// miss is:
//   .   +
//    \ /
//     a*
// (and its reflection).
// That is what this test checks.
// Sorry it's so code-duplication-iferous.  Refactors would be good...

namespace
{
  // this scalar represents an attr whose node may or may not already exist
  template <typename T>
  struct X_attr_mixed_scalar : public a_scalar
  {
    virtual string my_type() const { return "X_attr_scalar"; }

    map<scalar_val, pair<bool, attr_value> > values;
    X_attr_mixed_scalar(node_id_source & nis)
      : a_scalar(nis)
    {
      safe_insert(values, make_pair(scalar_a, make_pair(true, attr_value("a"))));
      safe_insert(values, make_pair(scalar_b, make_pair(true, attr_value("b"))));
      safe_insert(values, make_pair(scalar_c, make_pair(true, attr_value("c"))));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      // scalar_none is . in the above notation
      // and scalar_none_2 is +
      if (val != scalar_none)
        {
          T::make_obj(scalar_origin_rid, obj_under_test_nid, roster, markings);
          roster.attach_node(obj_under_test_nid, file_path_internal("foo"));
        }
      if (val != scalar_none && val != scalar_none_2)
        {
          safe_insert(roster.get_node_for_update(obj_under_test_nid)->attrs,
                      make_pair(attr_key("test_key"), safe_get(values, val)));
          markings.get_marking_for_update(obj_under_test_nid)->attrs[attr_key("test_key")] = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };
}

UNIT_TEST(residual_attr_mark_scenario)
{
  L(FL("TEST: begin checking residual attr marking case"));
  {
    testing_node_id_source nis;
    X_attr_mixed_scalar<file_maker> s(nis);
    run_with_2_roster_parents(s, left_rid,
                              scalar_none_2, set<revision_id>(),
                              scalar_none, set<revision_id>(),
                              scalar_a, singleton(new_rid),
                              nis);
  }
  {
    testing_node_id_source nis;
    X_attr_mixed_scalar<dir_maker> s(nis);
    run_with_2_roster_parents(s, left_rid,
                              scalar_none_2, set<revision_id>(),
                              scalar_none, set<revision_id>(),
                              scalar_a, singleton(new_rid),
                              nis);
  }
  {
    testing_node_id_source nis;
    X_attr_mixed_scalar<file_maker> s(nis);
    run_with_2_roster_parents(s, right_rid,
                              scalar_none, set<revision_id>(),
                              scalar_none_2, set<revision_id>(),
                              scalar_a, singleton(new_rid),
                              nis);
  }
  {
    testing_node_id_source nis;
    X_attr_mixed_scalar<dir_maker> s(nis);
    run_with_2_roster_parents(s, right_rid,
                              scalar_none, set<revision_id>(),
                              scalar_none_2, set<revision_id>(),
                              scalar_a, singleton(new_rid),
                              nis);
  }
  L(FL("TEST: end checking residual attr marking case"));
}

////////////////////////////////////////////////////////////////////////
// end of exhaustive tests
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// lifecyle tests
////////////////////////////////////////////////////////////////////////

// nodes can't survive dying on one side of a merge
UNIT_TEST(die_die_die_merge)
{
  roster_t left_roster; MM(left_roster);
  marking_map left_markings; MM(left_markings);
  roster_t right_roster; MM(right_roster);
  marking_map right_markings; MM(right_markings);
  testing_node_id_source nis;

  // left roster is empty except for the root
  left_roster.attach_node(left_roster.create_dir_node(nis), file_path());
  marking_t an_old_marking(new marking());
  an_old_marking->birth_revision = old_rid;
  an_old_marking->parent_name = singleton(old_rid);
  left_markings.put_marking(left_roster.root()->self, an_old_marking);

  // right roster is identical, except for a dir created in the old rev
  right_roster = left_roster;
  right_markings = left_markings;
  right_roster.attach_node(right_roster.create_dir_node(nis),
                           file_path_internal("foo"));
  right_markings.put_marking(right_roster.get_node(file_path_internal("foo"))->self,
                             an_old_marking);

  left_roster.check_sane_against(left_markings);
  right_roster.check_sane_against(right_markings);

  cset left_cs; MM(left_cs);
  // we add the node
  left_cs.dirs_added.insert(file_path_internal("foo"));
  // we do nothing
  cset right_cs; MM(right_cs);

  roster_t new_roster; MM(new_roster);
  marking_map new_markings; MM(new_markings);

  // because the dir was created in the old rev, the left side has logically
  // seen it and killed it, so it needs to be dead in the result.
  UNIT_TEST_CHECK_THROW(
     make_roster_for_merge(left_rid, left_roster, left_markings, left_cs,
                           singleton(left_rid),
                           right_rid, right_roster, right_markings, right_cs,
                           singleton(right_rid),
                           new_rid, new_roster, new_markings,
                           nis),
     logic_error);
  UNIT_TEST_CHECK_THROW(
     make_roster_for_merge(right_rid, right_roster, right_markings, right_cs,
                           singleton(right_rid),
                           left_rid, left_roster, left_markings, left_cs,
                           singleton(left_rid),
                           new_rid, new_roster, new_markings,
                           nis),
     logic_error);
}
// nodes can't change type file->dir or dir->file
//    make_cset fails
//    merging a file and a dir with the same nid and no mention of what should
//      happen to them fails

UNIT_TEST(same_nid_diff_type)
{
  randomizer rng;
  testing_node_id_source nis;

  roster_t dir_roster; MM(dir_roster);
  marking_map dir_markings; MM(dir_markings);
  dir_roster.attach_node(dir_roster.create_dir_node(nis), file_path());
  marking_t m(new marking());
  m->birth_revision = old_rid;
  m->parent_name = singleton(old_rid);
  dir_markings.put_marking(dir_roster.root()->self, marking_t(new marking(*m)));

  roster_t file_roster; MM(file_roster);
  marking_map file_markings; MM(file_markings);
  file_roster = dir_roster;
  file_markings = dir_markings;

  // okay, they both have the root dir
  node_id nid = nis.next();
  dir_roster.create_dir_node(nid);
  dir_roster.attach_node(nid, file_path_internal("foo"));
  dir_markings.put_marking(nid, marking_t(new marking(*m)));

  file_roster.create_file_node(new_ident(rng), nid);
  file_roster.attach_node(nid, file_path_internal("foo"));
  m->file_content = singleton(old_rid);
  file_markings.put_marking(nid, marking_t(new marking(*m)));

  dir_roster.check_sane_against(dir_markings);
  file_roster.check_sane_against(file_markings);

  cset cs; MM(cs);
  UNIT_TEST_CHECK_THROW(make_cset(dir_roster, file_roster, cs), logic_error);
  UNIT_TEST_CHECK_THROW(make_cset(file_roster, dir_roster, cs), logic_error);

  cset left_cs; MM(left_cs);
  cset right_cs; MM(right_cs);
  roster_t new_roster; MM(new_roster);
  marking_map new_markings; MM(new_markings);
  UNIT_TEST_CHECK_THROW(
     make_roster_for_merge(left_rid, dir_roster, dir_markings, left_cs,
                           singleton(left_rid),
                           right_rid, file_roster, file_markings, right_cs,
                           singleton(right_rid),
                           new_rid, new_roster, new_markings,
                           nis),
     logic_error);
  UNIT_TEST_CHECK_THROW(
     make_roster_for_merge(left_rid, file_roster, file_markings, left_cs,
                           singleton(left_rid),
                           right_rid, dir_roster, dir_markings, right_cs,
                           singleton(right_rid),
                           new_rid, new_roster, new_markings,
                           nis),
     logic_error);

}

UNIT_TEST(write_roster)
{
  L(FL("TEST: write_roster_test"));
  roster_t r; MM(r);
  marking_map mm; MM(mm);

  testing_node_id_source nis;

  file_path root;
  file_path foo = file_path_internal("foo");
  file_path foo_ang = file_path_internal("foo/ang");
  file_path foo_bar = file_path_internal("foo/bar");
  file_path foo_zoo = file_path_internal("foo/zoo");
  file_path fo = file_path_internal("fo");
  file_path xx = file_path_internal("xx");

  file_id f1(string(constants::idlen_bytes, '\x11'), origin::internal);
  revision_id rid(string(constants::idlen_bytes, '\x44'), origin::internal);
  node_id nid;

  // if adding new nodes, add them at the end to keep the node_id order

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, root);
  mark_new_node(rid, r.get_node(nid), mm);

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, foo);
  mark_new_node(rid, r.get_node(nid), mm);

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, xx);
  r.set_attr(xx, attr_key("say"), attr_value("hello"));
  mark_new_node(rid, r.get_node(nid), mm);

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, fo);
  mark_new_node(rid, r.get_node(nid), mm);

  // check that files aren't ordered separately to dirs & vice versa
  nid = nis.next();
  r.create_file_node(f1, nid);
  r.attach_node(nid, foo_bar);
  r.set_attr(foo_bar, attr_key("fascist"), attr_value("tidiness"));
  mark_new_node(rid, r.get_node(nid), mm);

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, foo_ang);
  mark_new_node(rid, r.get_node(nid), mm);

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, foo_zoo);
  r.set_attr(foo_zoo, attr_key("regime"), attr_value("new"));
  r.clear_attr(foo_zoo, attr_key("regime"));
  mark_new_node(rid, r.get_node(nid), mm);

  {
    // manifest first
    manifest_data mdat; MM(mdat);
    write_manifest_of_roster(r, mdat);

    manifest_data
      expected(string("format_version \"1\"\n"
                      "\n"
                      "dir \"\"\n"
                      "\n"
                      "dir \"fo\"\n"
                      "\n"
                      "dir \"foo\"\n"
                      "\n"
                      "dir \"foo/ang\"\n"
                      "\n"
                      "   file \"foo/bar\"\n"
                      "content [1111111111111111111111111111111111111111]\n"
                      "   attr \"fascist\" \"tidiness\"\n"
                      "\n"
                      "dir \"foo/zoo\"\n"
                      "\n"
                      " dir \"xx\"\n"
                      "attr \"say\" \"hello\"\n"
                      ), origin::internal);
    MM(expected);

    UNIT_TEST_CHECK_NOT_THROW( I(expected == mdat), logic_error);
  }

  {
    // full roster with local parts
    roster_data rdat; MM(rdat);
    write_roster_and_marking(r, mm, rdat);

    // node_id order is a hassle.
    // root 1, foo 2, xx 3, fo 4, foo_bar 5, foo_ang 6, foo_zoo 7
    roster_data
      expected(string("format_version \"1\"\n"
                      "\n"
                      "      dir \"\"\n"
                      "    ident \"1\"\n"
                      "    birth [4444444444444444444444444444444444444444]\n"
                      "path_mark [4444444444444444444444444444444444444444]\n"
                      "\n"
                      "      dir \"fo\"\n"
                      "    ident \"4\"\n"
                      "    birth [4444444444444444444444444444444444444444]\n"
                      "path_mark [4444444444444444444444444444444444444444]\n"
                      "\n"
                      "      dir \"foo\"\n"
                      "    ident \"2\"\n"
                      "    birth [4444444444444444444444444444444444444444]\n"
                      "path_mark [4444444444444444444444444444444444444444]\n"
                      "\n"
                      "      dir \"foo/ang\"\n"
                      "    ident \"6\"\n"
                      "    birth [4444444444444444444444444444444444444444]\n"
                      "path_mark [4444444444444444444444444444444444444444]\n"
                      "\n"
                      "        file \"foo/bar\"\n"
                      "     content [1111111111111111111111111111111111111111]\n"
                      "       ident \"5\"\n"
                      "        attr \"fascist\" \"tidiness\"\n"
                      "       birth [4444444444444444444444444444444444444444]\n"
                      "   path_mark [4444444444444444444444444444444444444444]\n"
                      "content_mark [4444444444444444444444444444444444444444]\n"
                      "   attr_mark \"fascist\" [4444444444444444444444444444444444444444]\n"
                      "\n"
                      "         dir \"foo/zoo\"\n"
                      "       ident \"7\"\n"
                      "dormant_attr \"regime\"\n"
                      "       birth [4444444444444444444444444444444444444444]\n"
                      "   path_mark [4444444444444444444444444444444444444444]\n"
                      "   attr_mark \"regime\" [4444444444444444444444444444444444444444]\n"
                      "\n"
                      "      dir \"xx\"\n"
                      "    ident \"3\"\n"
                      "     attr \"say\" \"hello\"\n"
                      "    birth [4444444444444444444444444444444444444444]\n"
                      "path_mark [4444444444444444444444444444444444444444]\n"
                      "attr_mark \"say\" [4444444444444444444444444444444444444444]\n"
                      ), origin::internal);
    MM(expected);

    UNIT_TEST_CHECK_NOT_THROW( I(expected == rdat), logic_error);
  }
}

UNIT_TEST(check_sane_against)
{
  testing_node_id_source nis;
  file_path root;
  file_path foo = file_path_internal("foo");
  file_path bar = file_path_internal("bar");

  file_id f1(decode_hexenc_as<file_id>("1111111111111111111111111111111111111111",
                                       origin::internal));
  revision_id rid(decode_hexenc_as<revision_id>("1234123412341234123412341234123412341234",
                                                origin::internal));
  node_id nid;

  {
    L(FL("TEST: check_sane_against_test, no extra nodes in rosters"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, bar);
    // missing the marking

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, no extra nodes in markings"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, bar);
    mark_new_node(rid, r.get_node(nid), mm);
    r.detach_node(bar);

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, missing birth rev"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm);
    mm.get_marking_for_update(nid)->birth_revision = revision_id();

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, missing path mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm);
    mm.get_marking_for_update(nid)->parent_name.clear();

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, missing content mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm);

    nid = nis.next();
    r.create_file_node(f1, nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm);
    mm.get_marking_for_update(nid)->file_content.clear();

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, extra content mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm);
    mm.get_marking_for_update(nid)->file_content.insert(rid);

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, missing attr mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    // NB: mark and _then_ add attr
    mark_new_node(rid, r.get_node(nid), mm);
    r.set_attr(root, attr_key("my_key"), attr_value("my_value"));

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, empty attr mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    r.set_attr(root, attr_key("my_key"), attr_value("my_value"));
    mark_new_node(rid, r.get_node(nid), mm);
    mm.get_marking_for_update(nid)->attrs[attr_key("my_key")].clear();

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, extra attr mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    r.set_attr(root, attr_key("my_key"), attr_value("my_value"));
    mark_new_node(rid, r.get_node(nid), mm);
    mm.get_marking_for_update(nid)->attrs[attr_key("my_second_key")].insert(rid);

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }
}

static void
check_post_roster_unification_ok(roster_t const & left,
                                 roster_t const & right,
                                 bool temp_nodes_ok)
{
  MM(left);
  MM(right);
  I(left == right);
  left.check_sane(temp_nodes_ok);
  right.check_sane(temp_nodes_ok);
}

static void
create_random_unification_task(roster_t & left,
                               roster_t & right,
                               editable_roster_base & left_erb,
                               editable_roster_base & right_erb,
                               editable_roster_for_merge & left_erm,
                               editable_roster_for_merge & right_erm,
                               randomizer & rng)
{
  size_t n_nodes = 20 + rng.uniform(60);

  // Stick in a root if there isn't one.
  if (!left.has_root())
    {
      I(!right.has_root());

      node_id left_nid = left_erm.create_dir_node();
      left_erm.attach_node(left_nid, file_path());

      node_id right_nid = right_erm.create_dir_node();
      right_erm.attach_node(right_nid, file_path());
    }

  // Now throw in a bunch of others
  for (size_t i = 0; i < n_nodes; ++i)
    {
      node_t left_n = random_element(left.all_nodes(), rng)->second;

      // With equal probability, choose to make the new node appear to
      // be new in just the left, just the right, or both.
      editable_roster_base * left_er;
      editable_roster_base * right_er;
      switch (rng.uniform(2))
        {
        case 0: left_er = &left_erm; right_er = &right_erm; break;
        case 1: left_er = &left_erb; right_er = &right_erm; break;
        case 2: left_er = &left_erm; right_er = &right_erb; break;
        default: I(false);
        }

      node_id left_nid, right_nid;
      if (rng.flip())
        {
          left_nid = left_er->create_dir_node();
          right_nid = right_er->create_dir_node();
        }
      else
        {
          file_id fid = new_ident(rng);
          left_nid = left_er->create_file_node(fid);
          right_nid = right_er->create_file_node(fid);
        }

      file_path pth;
      left.get_name(left_n->self, pth);
      I(right.has_node(pth));

      if (is_file_t(left_n) || (pth.depth() > 1 && rng.flip()))
        // Add a sibling of an existing entry.
        pth = pth.dirname() / new_component(rng);
      else
        // Add a child of an existing entry.
        pth = pth / new_component(rng);

      left_er->attach_node(left_nid, pth);
      right_er->attach_node(right_nid, pth);
    }
}

static void
unify_rosters_randomized_core(node_id_source & tmp_nis,
                              node_id_source & test_nis,
                              bool temp_nodes_ok)
{
  roster_t left, right;
  randomizer rng;
  for (size_t i = 0; i < 30; ++i)
    {
      editable_roster_base left_erb(left, test_nis);
      editable_roster_base right_erb(right, test_nis);
      editable_roster_for_merge left_erm(left, tmp_nis);
      editable_roster_for_merge right_erm(right, tmp_nis);

      create_random_unification_task(left, right,
                                     left_erb, right_erb,
                                     left_erm, right_erm, rng);
      unify_rosters(left, left_erm.new_nodes,
                    right, right_erm.new_nodes,
                    test_nis);
      check_post_roster_unification_ok(left, right, temp_nodes_ok);
    }
}

UNIT_TEST(unify_rosters_randomized_trueids)
{
  L(FL("TEST: begin checking unification of rosters (randomly, true IDs)"));
  temp_node_id_source tmp_nis;
  testing_node_id_source test_nis;
  unify_rosters_randomized_core(tmp_nis, test_nis, false);
  L(FL("TEST: end checking unification of rosters (randomly, true IDs)"));
}

UNIT_TEST(unify_rosters_randomized_tempids)
{
  L(FL("TEST: begin checking unification of rosters (randomly, temp IDs)"));
  temp_node_id_source tmp_nis;
  unify_rosters_randomized_core(tmp_nis, tmp_nis, true);
  L(FL("TEST: end checking unification of rosters (randomly, temp IDs)"));
}

UNIT_TEST(unify_rosters_end_to_end_ids)
{
  L(FL("TEST: begin checking unification of rosters (end to end, ids)"));
  revision_id has_rid = left_rid;
  revision_id has_not_rid = right_rid;
  file_id my_fid(decode_hexenc_as<file_id>("9012901290129012901290129012901290129012",
                                           origin::internal));

  testing_node_id_source nis;

  roster_t has_not_roster; MM(has_not_roster);
  marking_map has_not_markings; MM(has_not_markings);
  {
    has_not_roster.attach_node(has_not_roster.create_dir_node(nis),
                               file_path());
    marking_t root_marking(new marking());
    root_marking->birth_revision = old_rid;
    root_marking->parent_name = singleton(old_rid);
    has_not_markings.put_marking(has_not_roster.root()->self, root_marking);
  }

  roster_t has_roster = has_not_roster; MM(has_roster);
  marking_map has_markings = has_not_markings; MM(has_markings);
  node_id new_id;
  {
    new_id = has_roster.create_file_node(my_fid, nis);
    has_roster.attach_node(new_id, file_path_internal("foo"));
    marking_t file_marking(new marking());
    file_marking->birth_revision = has_rid;
    file_marking->parent_name = file_marking->file_content = singleton(has_rid);
    has_markings.put_marking(new_id, file_marking);
  }

  cset add_cs; MM(add_cs);
  safe_insert(add_cs.files_added, make_pair(file_path_internal("foo"), my_fid));
  cset no_add_cs; MM(no_add_cs);

  // added in left, then merged
  {
    roster_t new_roster; MM(new_roster);
    marking_map new_markings; MM(new_markings);
    make_roster_for_merge(has_rid, has_roster, has_markings, no_add_cs,
                          singleton(has_rid),
                          has_not_rid, has_not_roster, has_not_markings, add_cs,
                          singleton(has_not_rid),
                          new_rid, new_roster, new_markings,
                          nis);
    I(new_roster.get_node(file_path_internal("foo"))->self == new_id);
  }
  // added in right, then merged
  {
    roster_t new_roster; MM(new_roster);
    marking_map new_markings; MM(new_markings);
    make_roster_for_merge(has_not_rid, has_not_roster, has_not_markings, add_cs,
                          singleton(has_not_rid),
                          has_rid, has_roster, has_markings, no_add_cs,
                          singleton(has_rid),
                          new_rid, new_roster, new_markings,
                          nis);
    I(new_roster.get_node(file_path_internal("foo"))->self == new_id);
  }
  // added in merge
  // this is a little "clever", it uses the same has_not_roster twice, but the
  // second time it passes the has_rid, to make it a possible graph.
  {
    roster_t new_roster; MM(new_roster);
    marking_map new_markings; MM(new_markings);
    make_roster_for_merge(has_not_rid, has_not_roster, has_not_markings, add_cs,
                          singleton(has_not_rid),
                          has_rid, has_not_roster, has_not_markings, add_cs,
                          singleton(has_rid),
                          new_rid, new_roster, new_markings,
                          nis);
    I(new_roster.get_node(file_path_internal("foo"))->self
      != has_roster.get_node(file_path_internal("foo"))->self);
  }
  L(FL("TEST: end checking unification of rosters (end to end, ids)"));
}

UNIT_TEST(unify_rosters_end_to_end_attr_corpses)
{
  L(FL("TEST: begin checking unification of rosters (end to end, attr corpses)"));
  revision_id first_rid = left_rid;
  revision_id second_rid = right_rid;
  file_id my_fid(decode_hexenc_as<file_id>("9012901290129012901290129012901290129012",
                                           origin::internal));

  testing_node_id_source nis;

  // Both rosters have the file "foo"; in one roster, it has the attr corpse
  // "testfoo1", and in the other, it has the attr corpse "testfoo2".  Only
  // the second roster has the file "bar"; it has the attr corpse "testbar".

  roster_t first_roster; MM(first_roster);
  marking_map first_markings; MM(first_markings);
  node_id foo_id;
  {
    first_roster.attach_node(first_roster.create_dir_node(nis), file_path());
    marking_t m(new marking());
    m->birth_revision = old_rid;
    m->parent_name = singleton(old_rid);
    first_markings.put_marking(first_roster.root()->self, m);

    m.reset(new marking(*m));
    foo_id = first_roster.create_file_node(my_fid, nis);
    first_roster.attach_node(foo_id, file_path_internal("foo"));
    m->file_content = singleton(old_rid);
    first_markings.put_marking(first_roster.get_node(file_path_internal("foo"))->self, m);
  }

  roster_t second_roster = first_roster; MM(second_roster);
  marking_map second_markings = first_markings; MM(second_markings);
  {
    second_roster.attach_node(second_roster.create_file_node(my_fid, nis),
                              file_path_internal("bar"));
    safe_insert(second_roster.get_node_for_update(file_path_internal("bar"))->attrs,
                make_pair(attr_key("testbar"), make_pair(false, attr_value())));
    marking_t m(new marking());
    m->birth_revision = second_rid;
    m->parent_name = m->file_content = singleton(second_rid);
    safe_insert(m->attrs,
                make_pair(attr_key("testbar"), singleton(second_rid)));
    second_markings.put_marking(second_roster.get_node(file_path_internal("bar"))->self, m);
  }

  // put in the attrs on foo
  {
    safe_insert(first_roster.get_node_for_update(foo_id)->attrs,
                make_pair(attr_key("testfoo1"), make_pair(false, attr_value())));
    safe_insert(first_markings.get_marking_for_update(foo_id)->attrs,
                make_pair(attr_key("testfoo1"), singleton(first_rid)));
    safe_insert(second_roster.get_node_for_update(foo_id)->attrs,
                make_pair(attr_key("testfoo2"), make_pair(false, attr_value())));
    safe_insert(second_markings.get_marking_for_update(foo_id)->attrs,
                make_pair(attr_key("testfoo2"), singleton(second_rid)));
  }

  cset add_cs; MM(add_cs);
  safe_insert(add_cs.files_added, make_pair(file_path_internal("bar"), my_fid));
  cset no_add_cs; MM(no_add_cs);

  {
    roster_t new_roster; MM(new_roster);
    marking_map new_markings; MM(new_markings);
    make_roster_for_merge(first_rid, first_roster, first_markings, add_cs,
                          singleton(first_rid),
                          second_rid, second_roster, second_markings, no_add_cs,
                          singleton(second_rid),
                          new_rid, new_roster, new_markings,
                          nis);
    I(new_roster.get_node(file_path_internal("foo"))->attrs.size() == 2);
    I(new_roster.get_node(file_path_internal("bar"))->attrs
      == second_roster.get_node(file_path_internal("bar"))->attrs);
    I(new_roster.get_node(file_path_internal("bar"))->attrs.size() == 1);
  }
  {
    roster_t new_roster; MM(new_roster);
    marking_map new_markings; MM(new_markings);
    make_roster_for_merge(second_rid, second_roster, second_markings, no_add_cs,
                          singleton(second_rid),
                          first_rid, first_roster, first_markings, add_cs,
                          singleton(first_rid),
                          new_rid, new_roster, new_markings,
                          nis);
    I(new_roster.get_node(file_path_internal("foo"))->attrs.size() == 2);
    I(new_roster.get_node(file_path_internal("bar"))->attrs
      == second_roster.get_node(file_path_internal("bar"))->attrs);
    I(new_roster.get_node(file_path_internal("bar"))->attrs.size() == 1);
  }

  L(FL("TEST: end checking unification of rosters (end to end, attr corpses)"));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
