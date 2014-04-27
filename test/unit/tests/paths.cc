// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//               2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "../../../src/base.hh"
#include "../unit_tests.hh"
#include "../randomizer.hh"

#include "../../../src/paths.cc"

using std::logic_error;

UNIT_TEST(path_component)
{
  char const * const baddies[] = {".",
                            "..",
                            "/foo",
                            "\\foo",
                            "foo/bar",
                            "foo\\bar",
                            0 };

  // these would not be okay in a full file_path, but are okay here.
  char const * const goodies[] = {"c:foo",
                            "_mtn",
                            "_mtN",
                            "_mTn",
                            "_Mtn",
                            "_MTn",
                            "_MtN",
                            "_MTN",
                            0 };


  for (char const * const * c = baddies; *c; ++c)
    {
      // the comparison prevents the compiler from eliminating the
      // expression.
      UNIT_TEST_CHECK_THROW((path_component(*c)()) == *c, logic_error);
    }
  for (char const * const *c = goodies; *c; ++c)
    {
      path_component p(*c);
      UNIT_TEST_CHECK_THROW(file_path() / p, logic_error);
    }

  UNIT_TEST_CHECK_THROW(file_path_internal("foo") / path_component(),
                        logic_error);
}


UNIT_TEST(file_path_internal)
{
  char const * const baddies[] = {"/foo",
                            "foo//bar",
                            "foo/../bar",
                            "../bar",
                            "_MTN",
                            "_MTN/blah",
                            "foo/bar/",
                            "foo/bar/.",
                            "foo/bar/./",
                            "foo/./bar",
                            "./foo",
                            ".",
                            "..",
                            "c:\\foo",
                            "c:foo",
                            "c:/foo",
                            // some baddies made bad by a security kluge --
                            // see the comment in in_bookkeeping_dir
                            "_mtn",
                            "_mtN",
                            "_mTn",
                            "_Mtn",
                            "_MTn",
                            "_MtN",
                            "_mTN",
                            "_mtn/foo",
                            "_mtN/foo",
                            "_mTn/foo",
                            "_Mtn/foo",
                            "_MTn/foo",
                            "_MtN/foo",
                            "_mTN/foo",
                            0 };
  initial_rel_path.unset();
  initial_rel_path.set(string(), true);
  for (char const * const * c = baddies; *c; ++c)
    {
      UNIT_TEST_CHECK_THROW(file_path_internal(*c), logic_error);
    }
  initial_rel_path.unset();
  initial_rel_path.set("blah/blah/blah", true);
  for (char const * const * c = baddies; *c; ++c)
    {
      UNIT_TEST_CHECK_THROW(file_path_internal(*c), logic_error);
    }

  UNIT_TEST_CHECK(file_path().empty());
  UNIT_TEST_CHECK(file_path_internal("").empty());

  char const * const goodies[] = {"",
                            "a",
                            "foo",
                            "foo/bar/baz",
                            "foo/bar.baz",
                            "foo/with-hyphen/bar",
                            "foo/with_underscore/bar",
                            "foo/with,other+@weird*%#$=stuff/bar",
                            ".foo/bar",
                            "..foo/bar",
                            "_MTNfoo/bar",
                            "foo:bar",
                            0 };

  for (int i = 0; i < 2; ++i)
    {
      initial_rel_path.unset();
      initial_rel_path.set(i ? string()
                             : string("blah/blah/blah"),
                           true);
      for (char const * const * c = goodies; *c; ++c)
        {
          file_path fp = file_path_internal(*c);
          UNIT_TEST_CHECK(fp.as_internal() == *c);
          UNIT_TEST_CHECK(file_path_internal(fp.as_internal()) == fp);
        }
    }

  initial_rel_path.unset();
}

static void check_fp_normalizes_to(char const * before, char const * after)
{
  L(FL("check_fp_normalizes_to: '%s' -> '%s'") % before % after);
  file_path fp = file_path_external(utf8(before));
  L(FL("  (got: %s)") % fp);
  UNIT_TEST_CHECK(fp.as_internal() == after);
  UNIT_TEST_CHECK(file_path_internal(fp.as_internal()) == fp);
  // we compare after to the external form too, since as far as we know
  // relative normalized posix paths are always good win32 paths too
  UNIT_TEST_CHECK(fp.as_external() == after);
}

UNIT_TEST(file_path_external_null_prefix)
{
  initial_rel_path.unset();
  initial_rel_path.set(string(), true);

  char const * const baddies[] = {"/foo",
                            "../bar",
                            "_MTN/blah",
                            "_MTN",
                            "//blah",
                            "\\foo",
                            "..",
                            "c:\\foo",
                            "c:foo",
                            "c:/foo",
                            // some baddies made bad by a security kluge --
                            // see the comment in in_bookkeeping_dir
                            "_mtn",
                            "_mtN",
                            "_mTn",
                            "_Mtn",
                            "_MTn",
                            "_MtN",
                            "_mTN",
                            "_mtn/foo",
                            "_mtN/foo",
                            "_mTn/foo",
                            "_Mtn/foo",
                            "_MTn/foo",
                            "_MtN/foo",
                            "_mTN/foo",
                            0 };
  for (char const * const * c = baddies; *c; ++c)
    {
      L(FL("test_file_path_external_null_prefix: trying baddie: %s") % *c);
      UNIT_TEST_CHECK_THROW(file_path_external(utf8(*c)), recoverable_failure);
    }

  check_fp_normalizes_to("a", "a");
  check_fp_normalizes_to("foo", "foo");
  check_fp_normalizes_to("foo/bar", "foo/bar");
  check_fp_normalizes_to("foo/bar/baz", "foo/bar/baz");
  check_fp_normalizes_to("foo/bar.baz", "foo/bar.baz");
  check_fp_normalizes_to("foo/with-hyphen/bar", "foo/with-hyphen/bar");
  check_fp_normalizes_to("foo/with_underscore/bar", "foo/with_underscore/bar");
  check_fp_normalizes_to(".foo/bar", ".foo/bar");
  check_fp_normalizes_to("..foo/bar", "..foo/bar");
  check_fp_normalizes_to(".", "");
  check_fp_normalizes_to("", "");
#ifndef WIN32
  check_fp_normalizes_to("foo:bar", "foo:bar");
#endif
  check_fp_normalizes_to("foo/with,other+@weird*%#$=stuff/bar",
                         "foo/with,other+@weird*%#$=stuff/bar");

  check_fp_normalizes_to("foo//bar", "foo/bar");
  check_fp_normalizes_to("foo/../bar", "bar");
  check_fp_normalizes_to("foo/bar/", "foo/bar");
  check_fp_normalizes_to("foo/bar/.", "foo/bar");
  check_fp_normalizes_to("foo/bar/./", "foo/bar");
  check_fp_normalizes_to("foo/./bar/", "foo/bar");
  check_fp_normalizes_to("./foo", "foo");
  check_fp_normalizes_to("foo///.//", "foo");

  initial_rel_path.unset();
}

UNIT_TEST(file_path_external_prefix__MTN)
{
  initial_rel_path.unset();
  initial_rel_path.set(string("_MTN"), true);

  UNIT_TEST_CHECK_THROW(file_path_external(utf8("foo")), recoverable_failure);
  UNIT_TEST_CHECK_THROW(file_path_external(utf8(".")), recoverable_failure);
  UNIT_TEST_CHECK_THROW(file_path_external(utf8("./blah")), recoverable_failure);
  check_fp_normalizes_to("..", "");
  check_fp_normalizes_to("../foo", "foo");
}

UNIT_TEST(file_path_external_prefix_a_b)
{
  initial_rel_path.unset();
  initial_rel_path.set(string("a/b"), true);

  char const * const baddies[] = {"/foo",
                            "../../../bar",
                            "../../..",
                            "../../_MTN",
                            "../../_MTN/foo",
                            "//blah",
                            "\\foo",
                            "c:\\foo",
#ifdef WIN32
                            "c:foo",
                            "c:/foo",
#endif
                            // some baddies made bad by a security kluge --
                            // see the comment in in_bookkeeping_dir
                            "../../_mtn",
                            "../../_mtN",
                            "../../_mTn",
                            "../../_Mtn",
                            "../../_MTn",
                            "../../_MtN",
                            "../../_mTN",
                            "../../_mtn/foo",
                            "../../_mtN/foo",
                            "../../_mTn/foo",
                            "../../_Mtn/foo",
                            "../../_MTn/foo",
                            "../../_MtN/foo",
                            "../../_mTN/foo",
                            0 };
  for (char const * const * c = baddies; *c; ++c)
    {
      L(FL("test_file_path_external_prefix_a_b: trying baddie: %s") % *c);
      UNIT_TEST_CHECK_THROW(file_path_external(utf8(*c)), recoverable_failure);
    }

  check_fp_normalizes_to("foo", "a/b/foo");
  check_fp_normalizes_to("a", "a/b/a");
  check_fp_normalizes_to("foo/bar", "a/b/foo/bar");
  check_fp_normalizes_to("foo/bar/baz", "a/b/foo/bar/baz");
  check_fp_normalizes_to("foo/bar.baz", "a/b/foo/bar.baz");
  check_fp_normalizes_to("foo/with-hyphen/bar", "a/b/foo/with-hyphen/bar");
  check_fp_normalizes_to("foo/with_underscore/bar", "a/b/foo/with_underscore/bar");
  check_fp_normalizes_to(".foo/bar", "a/b/.foo/bar");
  check_fp_normalizes_to("..foo/bar", "a/b/..foo/bar");
  check_fp_normalizes_to(".", "a/b");
  check_fp_normalizes_to("", "a/b");
#ifndef WIN32
  check_fp_normalizes_to("foo:bar", "a/b/foo:bar");
#endif
  check_fp_normalizes_to("foo/with,other+@weird*%#$=stuff/bar",
                         "a/b/foo/with,other+@weird*%#$=stuff/bar");
  check_fp_normalizes_to("foo//bar", "a/b/foo/bar");
  check_fp_normalizes_to("foo/../bar", "a/b/bar");
  check_fp_normalizes_to("foo/bar/", "a/b/foo/bar");
  check_fp_normalizes_to("foo/bar/.", "a/b/foo/bar");
  check_fp_normalizes_to("foo/bar/./", "a/b/foo/bar");
  check_fp_normalizes_to("foo/./bar/", "a/b/foo/bar");
  check_fp_normalizes_to("./foo", "a/b/foo");
  check_fp_normalizes_to("foo///.//", "a/b/foo");
  // things that would have been bad without the initial_rel_path:
  check_fp_normalizes_to("../foo", "a/foo");
  check_fp_normalizes_to("..", "a");
  check_fp_normalizes_to("../..", "");
  check_fp_normalizes_to("_MTN/foo", "a/b/_MTN/foo");
  check_fp_normalizes_to("_MTN", "a/b/_MTN");
#ifndef WIN32
  check_fp_normalizes_to("c:foo", "a/b/c:foo");
  check_fp_normalizes_to("c:/foo", "a/b/c:/foo");
#endif

  initial_rel_path.unset();
}

UNIT_TEST(basename)
{
  struct t
  {
    char const * in;
    char const * out;
  };
  // file_paths cannot be absolute, but may be the empty string.
  struct t const fp_cases[] = {
    { "",            ""    },
    { "foo",         "foo" },
    { "foo/bar",     "bar" },
    { "foo/bar/baz", "baz" },
    { 0, 0 }
  };
  // bookkeeping_paths cannot be absolute and must start with the
  // bookkeeping_root_component.
  struct t const bp_cases[] = {
    { "_MTN",         "_MTN" },
    { "_MTN/foo",     "foo"  },
    { "_MTN/foo/bar", "bar"  },
    { 0, 0 }
  };

  // system_paths must be absolute.  this relies on the setting of
  // initial_abs_path below.  note that most of the cases whose full paths
  // vary between Unix and Windows will still have the same basenames.
  struct t const sp_cases[] = {
    { "/",          ""      },
    { "//",         ""      },
    { "foo",        "foo"   },
    { "/foo",       "foo"   },
    { "//foo",      "foo"   },
    { "~/foo",      "foo"   },
    { "c:/foo",     "foo"   },
    { "foo/bar",    "bar"   },
    { "/foo/bar",   "bar"   },
    { "//foo/bar",  "bar"   },
    { "~/foo/bar",  "bar"   },
    { "c:/foo/bar", "bar"   },
#ifdef WIN32
    { "c:/",        ""      },
    { "c:foo",      "foo"   },
#else
    { "c:/",        "c:"    },
    { "c:foo",      "c:foo" },
#endif
    { 0, 0 }
  };

  UNIT_TEST_CHECKPOINT("file_path basenames");
  for (struct t const *p = fp_cases; p->in; p++)
    {
      file_path fp = file_path_internal(p->in);
      path_component pc(fp.basename());
      UNIT_TEST_CHECK_MSG(pc == path_component(p->out),
                          FL("basename('%s') = '%s' (expect '%s')")
                          % p->in % pc % p->out);
    }

  UNIT_TEST_CHECKPOINT("bookkeeping_path basenames");
  for (struct t const *p = bp_cases; p->in; p++)
    {
      bookkeeping_path fp(p->in);
      path_component pc(fp.basename());
      UNIT_TEST_CHECK_MSG(pc == path_component(p->out),
                          FL("basename('%s') = '%s' (expect '%s')")
                          % p->in % pc % p->out);
    }


  UNIT_TEST_CHECKPOINT("system_path basenames");

  initial_abs_path.unset();
  initial_abs_path.set(system_path("/a/b"), true);

  for (struct t const *p = sp_cases; p->in; p++)
    {
      system_path fp(p->in);
      path_component pc(fp.basename());
      UNIT_TEST_CHECK_MSG(pc == path_component(p->out),
                          FL("basename('%s') = '%s' (expect '%s')")
                          % p->in % pc % p->out);
    }

  // any_path::basename() should return exactly the same thing that
  // the corresponding specialized basename() does, but with type any_path.
  UNIT_TEST_CHECKPOINT("any_path basenames");
  for (struct t const *p = fp_cases; p->in; p++)
    {
      any_path ap(file_path_internal(p->in));
      path_component pc(ap.basename());
      UNIT_TEST_CHECK_MSG(pc == path_component(p->out),
                          FL("basename('%s') = '%s' (expect '%s')")
                          % p->in % pc % p->out);
    }
  for (struct t const *p = bp_cases; p->in; p++)
    {
      any_path ap(bookkeeping_path(p->in));
      path_component pc(ap.basename());
      UNIT_TEST_CHECK_MSG(pc == path_component(p->out),
                          FL("basename('%s') = '%s' (expect '%s')")
                          % p->in % pc % p->out);
    }
  for (struct t const *p = sp_cases; p->in; p++)
    {
      any_path ap(system_path(p->in));
      path_component pc(ap.basename());
      UNIT_TEST_CHECK_MSG(pc == path_component(p->out),
                          FL("basename('%s') = '%s' (expect '%s')")
                          % p->in % pc % p->out);
    }

  initial_abs_path.unset();
}

UNIT_TEST(dirname)
{
  struct t
  {
    char const * in;
    char const * out;
  };
  // file_paths cannot be absolute, but may be the empty string.
  struct t const fp_cases[] = {
    { "",            ""        },
    { "foo",         ""        },
    { "foo/bar",     "foo"     },
    { "foo/bar/baz", "foo/bar" },
    { 0, 0 }
  };

  // system_paths must be absolute.  this relies on the setting of
  // initial_abs_path below.
  struct t const sp_cases[] = {
    { "/",          "/"           },
    { "//",         "//"          },
    { "foo",        "/a/b"        },
    { "/foo",       "/"           },
    { "//foo",      "//"          },
    { "~/foo",      "~"           },
    { "foo/bar",    "/a/b/foo"    },
    { "/foo/bar",   "/foo"        },
    { "//foo/bar",  "//foo"       },
    { "~/foo/bar",  "~/foo"       },
#ifdef WIN32
    { "c:",         "c:"          },
    { "c:foo",      "c:"          },
    { "c:/",        "c:/"         },
    { "c:/foo",     "c:/"         },
    { "c:/foo/bar", "c:/foo"      },
#else
    { "c:",         "/a/b"        },
    { "c:foo",      "/a/b"        },
    { "c:/",        "/a/b"        },
    { "c:/foo",     "/a/b/c:"     },
    { "c:/foo/bar", "/a/b/c:/foo" },
#endif
    { 0, 0 }
  };

  initial_abs_path.unset();

  UNIT_TEST_CHECKPOINT("file_path dirnames");
  for (struct t const *p = fp_cases; p->in; p++)
    {
      file_path fp = file_path_internal(p->in);
      file_path dn = fp.dirname();
      UNIT_TEST_CHECK_MSG(dn == file_path_internal(p->out),
                          FL("dirname('%s') = '%s' (expect '%s')")
                          % p->in % dn % p->out);
    }


  initial_abs_path.set(system_path("/a/b"), true);
  UNIT_TEST_CHECKPOINT("system_path dirnames");
  for (struct t const *p = sp_cases; p->in; p++)
    {
      system_path fp(p->in);
      system_path dn(fp.dirname());

      UNIT_TEST_CHECK_MSG(dn == system_path(p->out),
                          FL("dirname('%s') = '%s' (expect '%s')")
                          % p->in % dn % p->out);
    }

  // any_path::dirname() should return exactly the same thing that
  // the corresponding specialized dirname() does, but with type any_path.
  UNIT_TEST_CHECKPOINT("any_path dirnames");
  for (struct t const *p = fp_cases; p->in; p++)
    {
      any_path ap(file_path_internal(p->in));
      any_path dn(ap.dirname());
      any_path rf(file_path_internal(p->out));
      UNIT_TEST_CHECK_MSG(dn.as_internal() == rf.as_internal(),
                          FL("dirname('%s') = '%s' (expect '%s')")
                          % p->in % dn % rf);
    }
  for (struct t const *p = sp_cases; p->in; p++)
    {
      any_path ap(system_path(p->in));
      any_path dn(ap.dirname());
      any_path rf(system_path(p->out));
      UNIT_TEST_CHECK_MSG(dn.as_internal() == rf.as_internal(),
                          FL("dirname('%s') = '%s' (expect '%s')")
                          % p->in % dn % rf);
    }

  initial_abs_path.unset();
}

UNIT_TEST(depth)
{
  char const * const cases[] = {"", "foo", "foo/bar", "foo/bar/baz", 0};
  for (unsigned int i = 0; cases[i]; i++)
    {
      file_path fp = file_path_internal(cases[i]);
      unsigned int d = fp.depth();
      UNIT_TEST_CHECK_MSG(d == i,
                          FL("depth('%s') = %d (expect %d)") % fp % d % i);
    }
}

static void check_bk_normalizes_to(char const * before, char const * after)
{
  bookkeeping_path bp(bookkeeping_root / before);
  L(FL("normalizing %s to %s (got %s)") % before % after % bp);
  UNIT_TEST_CHECK(bp.as_external() == after);
  UNIT_TEST_CHECK(bookkeeping_path(bp.as_internal(),
                                   origin::internal).as_internal() == bp.as_internal());
}

UNIT_TEST(bookkeeping)
{
  char const * const baddies[] = {"/foo",
                            "foo//bar",
                            "foo/../bar",
                            "../bar",
                            "foo/bar/",
                            "foo/bar/.",
                            "foo/bar/./",
                            "foo/./bar",
                            "./foo",
                            ".",
                            "..",
                            "c:\\foo",
                            "c:foo",
                            "c:/foo",
                            "",
                            "a:b",
                            0 };
  string tmp_path_string;

  for (char const * const * c = baddies; *c; ++c)
    {
      L(FL("test_bookkeeping_path baddie: trying '%s'") % *c);
      UNIT_TEST_CHECK_THROW(bookkeeping_path(tmp_path_string.assign(*c),
                                             origin::internal),
                            logic_error);
      UNIT_TEST_CHECK_THROW(bookkeeping_root / *c, logic_error);
    }

  // these are legitimate as things to append to bookkeeping_root, but
  // not as bookkeeping_paths in themselves.
  UNIT_TEST_CHECK_THROW(bookkeeping_path("a"), logic_error);
  UNIT_TEST_CHECK_NOT_THROW(bookkeeping_root / "a", logic_error);
  UNIT_TEST_CHECK_THROW(bookkeeping_path("foo/bar"), logic_error);
  UNIT_TEST_CHECK_NOT_THROW(bookkeeping_root / "foo/bar", logic_error);

  check_bk_normalizes_to("a", "_MTN/a");
  check_bk_normalizes_to("foo", "_MTN/foo");
  check_bk_normalizes_to("foo/bar", "_MTN/foo/bar");
  check_bk_normalizes_to("foo/bar/baz", "_MTN/foo/bar/baz");
}

static void check_system_normalizes_to(char const * before, char const * after)
{
  system_path sp(before);
  L(FL("normalizing '%s' to '%s' (got '%s')") % before % after % sp);
  UNIT_TEST_CHECK(sp.as_external() == after);
  UNIT_TEST_CHECK(system_path(sp.as_internal(),
                              origin::internal).as_internal() == sp.as_internal());
}

UNIT_TEST(system)
{
  initial_abs_path.unset();
  initial_abs_path.set(system_path("/a/b"), true);

  UNIT_TEST_CHECK_THROW(system_path(""), unrecoverable_failure);

  check_system_normalizes_to("foo", "/a/b/foo");
  check_system_normalizes_to("foo/bar", "/a/b/foo/bar");
  check_system_normalizes_to("/foo/bar", "/foo/bar");
  check_system_normalizes_to("//foo/bar", "//foo/bar");
#ifdef WIN32
  check_system_normalizes_to("c:foo", "c:foo");
  check_system_normalizes_to("c:/foo", "c:/foo");
  check_system_normalizes_to("c:\\foo", "c:/foo");
#else
  check_system_normalizes_to("c:foo", "/a/b/c:foo");
  check_system_normalizes_to("c:/foo", "/a/b/c:/foo");
  check_system_normalizes_to("c:\\foo", "/a/b/c:\\foo");
  check_system_normalizes_to("foo:bar", "/a/b/foo:bar");
#endif
  // we require that system_path normalize out ..'s, because of the following
  // case:
  //   /work mkdir newdir
  //   /work$ cd newdir
  //   /work/newdir$ monotone setup --db=../foo.db
  // Now they have either "/work/foo.db" or "/work/newdir/../foo.db" in
  // _MTN/options
  //   /work/newdir$ cd ..
  //   /work$ mv newdir newerdir  # better name
  // Oops, now, if we stored the version with ..'s in, this workspace
  // is broken.
  check_system_normalizes_to("../foo", "/a/foo");
  check_system_normalizes_to("foo/..", "/a/b");
  check_system_normalizes_to("/foo/bar/..", "/foo");
  check_system_normalizes_to("/foo/..", "/");
  // can't do particularly interesting checking of tilde expansion, but at
  // least we can check that it's doing _something_...
  string tilde_expanded = system_path("~/foo").as_external();
#ifdef WIN32
  UNIT_TEST_CHECK(tilde_expanded[1] == ':');
#else
  UNIT_TEST_CHECK(tilde_expanded[0] == '/');
#endif
  // This test should not be performed, tildes are allowed in the middle
  // of paths, and we have already shown in the previous test that what
  // we expected happened (or didn't).
  //UNIT_TEST_CHECK(tilde_expanded.find('~') == string::npos);

  // on Windows, ~name is not expanded
#ifdef WIN32
  UNIT_TEST_CHECK(system_path("~this_user_does_not_exist_anywhere")
                  .as_external()
                  == "/a/b/~this_user_does_not_exist_anywhere");
#else
  UNIT_TEST_CHECK_THROW(system_path("~this_user_does_not_exist_anywhere"),
                        recoverable_failure);
#endif

  // finally, make sure that the copy-from-any_path constructor works right
  // in particular, it should interpret the paths it gets as being relative to
  // the project root, not the initial path
  working_root.unset();
  working_root.set(system_path("/working/root"), true);
  initial_rel_path.unset();
  initial_rel_path.set(string("rel/initial"), true);

  UNIT_TEST_CHECK(system_path(system_path("foo/bar")).as_internal() == "/a/b/foo/bar");
  UNIT_TEST_CHECK(!working_root.used);
  UNIT_TEST_CHECK(system_path(system_path("/foo/bar")).as_internal() == "/foo/bar");
  UNIT_TEST_CHECK(!working_root.used);
  UNIT_TEST_CHECK(system_path(file_path_internal("foo/bar"), false).as_internal()
              == "/working/root/foo/bar");
  UNIT_TEST_CHECK(!working_root.used);
  UNIT_TEST_CHECK(system_path(file_path_internal("foo/bar")).as_internal()
              == "/working/root/foo/bar");
  UNIT_TEST_CHECK(working_root.used);
  UNIT_TEST_CHECK(system_path(file_path_external(utf8("foo/bar"))).as_external()
              == "/working/root/rel/initial/foo/bar");
  file_path a_file_path;
  UNIT_TEST_CHECK(system_path(a_file_path).as_external()
              == "/working/root");
  UNIT_TEST_CHECK(system_path(bookkeeping_path("_MTN/foo/bar")).as_internal()
              == "/working/root/_MTN/foo/bar");
  UNIT_TEST_CHECK(system_path(bookkeeping_root).as_internal()
              == "/working/root/_MTN");
  initial_abs_path.unset();
  working_root.unset();
  initial_rel_path.unset();
}

UNIT_TEST(access_tracker)
{
  access_tracker<int> a;
  UNIT_TEST_CHECK_THROW(a.get(), logic_error);
  a.set(1, false);
  UNIT_TEST_CHECK_THROW(a.set(2, false), logic_error);
  a.set(2, true);
  UNIT_TEST_CHECK_THROW(a.set(3, false), logic_error);
  UNIT_TEST_CHECK(a.get() == 2);
  UNIT_TEST_CHECK_THROW(a.set(3, true), logic_error);
  a.unset();
  a.may_not_initialize();
  UNIT_TEST_CHECK_THROW(a.set(1, false), logic_error);
  UNIT_TEST_CHECK_THROW(a.set(2, true), logic_error);
  a.unset();
  a.set(1, false);
  UNIT_TEST_CHECK_THROW(a.may_not_initialize(), logic_error);
}

static void test_path_less_than(string const & left, string const & right)
{
  MM(left);
  MM(right);
  file_path left_fp = file_path_internal(left);
  file_path right_fp = file_path_internal(right);
  I(left_fp < right_fp);
}

static void test_path_equal(string const & left, string const & right)
{
  MM(left);
  MM(right);
  file_path left_fp = file_path_internal(left);
  file_path right_fp = file_path_internal(right);
  I(left_fp == right_fp);
}

UNIT_TEST(ordering)
{
  // this ordering is very important:
  //   -- it is used to determine the textual form of csets and manifests
  //      (in particular, it cannot be changed)
  //   -- it is used to determine in what order cset operations can be applied
  //      (in particular, foo must sort before foo/bar, so that we can use it
  //      to do top-down and bottom-up traversals of a set of paths).
  test_path_less_than("a", "b");
  test_path_less_than("a", "c");
  test_path_less_than("ab", "ac");
  test_path_less_than("a", "ab");
  test_path_less_than("", "a");
  test_path_less_than("", ".foo");
  test_path_less_than("foo", "foo/bar");
  // . is before / asciibetically, so sorting by strings will give the wrong
  // answer on this:
  test_path_less_than("foo/bar", "foo.bar");

  // path_components used to be interned strings, and we used the default sort
  // order, which meant that in practice path components would sort in the
  // _order they were first used in the program_.  So let's put in a test that
  // would catch this sort of brokenness.
  test_path_less_than("fallanopic_not_otherwise_mentioned", "xyzzy");
  test_path_less_than("fallanoooo_not_otherwise_mentioned_and_smaller",
                       "fallanopic_not_otherwise_mentioned");
}

UNIT_TEST(ordering_random)
{
  char x[4] = {0,0,0,0};
  char y[4] = {0,0,0,0};
  u8 a, b, c, d;
  const int ntrials = 1000;
  int i;
  randomizer rng;

  // use of numbers is intentional; these strings are defined to be UTF-8.

  UNIT_TEST_CHECKPOINT("a and b");
  for (i = 0; i < ntrials; i++)
    {
      do a = rng.uniform(0x7f - 0x20) + 0x20;
      while (a == 0x5c || a == 0x2f || a == 0x2e); // '\\', '/', '.'

      do b = rng.uniform(0x7f - 0x20) + 0x20;
      while (b == 0x5c || b == 0x2f || b == 0x2e); // '\\', '/', '.'

      x[0] = a;
      y[0] = b;
      if (a < b)
        test_path_less_than(x, y);
      else if (a > b)
        test_path_less_than(y, x);
      else
        test_path_equal(x, y);
    }

  UNIT_TEST_CHECKPOINT("ab and cd");
  for (i = 0; i < ntrials; i++)
    {
      do
        {
          do a = rng.uniform(0x7f - 0x20) + 0x20;
          while (a == 0x5c || a == 0x2f); // '\\', '/'

          do b = rng.uniform(0x7f - 0x20) + 0x20;
          while (b == 0x5c || b == 0x2f || b == 0x3a); // '\\', '/', ':'
        }
      while (a == 0x2e && b == 0x2e);  // ".."

      do
        {
          do c = rng.uniform(0x7f - 0x20) + 0x20;
          while (c == 0x5c || c == 0x2f); // '\\', '/'

          do d = rng.uniform(0x7f - 0x20) + 0x20;
          while (d == 0x5c || d == 0x2f || d == 0x3a); // '\\', '/', ':'
        }
      while (c == 0x2e && d == 0x2e);  // ".."

      x[0] = a;
      x[1] = b;
      y[0] = c;
      y[1] = d;

      if (a < c || (a == c && b < d))
        test_path_less_than(x, y);
      else if (a > c || (a == c && b > d))
        test_path_less_than(y, x);
      else
        test_path_equal(x, y);
    }

  UNIT_TEST_CHECKPOINT("a and b/c");
  x[1] = 0;
  y[1] = '/';
  for (i = 0; i < ntrials; i++)
    {
      do a = rng.uniform(0x7f - 0x20) + 0x20;
      while (a == 0x5c || a == 0x2f || a == 0x2e); // '\\', '/', '.'

      do b = rng.uniform(0x7f - 0x20) + 0x20;
      while (b == 0x5c || b == 0x2f || b == 0x2e); // '\\', '/', '.'

      do c = rng.uniform(0x7f - 0x20) + 0x20;
      while (c == 0x5c || c == 0x2f || c == 0x2e); // '\\', '/', '.'

      x[0] = a;
      y[0] = b;
      y[2] = c;

      // only the order of a and b matters.  1 sorts before 1/2.
      if (a <= b)
        test_path_less_than(x, y);
      else
        test_path_less_than(y, x);
    }

  UNIT_TEST_CHECKPOINT("ab and c/d");
  for (i = 0; i < ntrials; i++)
    {
      do
        {
          do a = rng.uniform(0x7f - 0x20) + 0x20;
          while (a == 0x5c || a == 0x2f); // '\\', '/'

          do b = rng.uniform(0x7f - 0x20) + 0x20;
          while (b == 0x5c || b == 0x2f || b == 0x3a); // '\\', '/', ':'
        }
      while (a == 0x2e && b == 0x2e);  // ".."

      do c = rng.uniform(0x7f - 0x20) + 0x20;
      while (c == 0x5c || c == 0x2f || c == 0x2e); // '\\', '/', '.'

      do d = rng.uniform(0x7f - 0x20) + 0x20;
      while (d == 0x5c || d == 0x2f || d == 0x2e); // '\\', '/', '.'


      x[0] = a;
      x[1] = b;
      y[0] = c;
      y[2] = d;

      // only the order of a and c matters,
      // but this time, 12 sorts after 1/2.
      if (a < c)
        test_path_less_than(x, y);
      else
        test_path_less_than(y, x);
    }


  UNIT_TEST_CHECKPOINT("a/b and c/d");
  x[1] = '/';
  for (i = 0; i < ntrials; i++)
    {
      do a = rng.uniform(0x7f - 0x20) + 0x20;
      while (a == 0x5c || a == 0x2f || a == 0x2e); // '\\', '/', '.'

      do b = rng.uniform(0x7f - 0x20) + 0x20;
      while (b == 0x5c || b == 0x2f || b == 0x2e); // '\\', '/', '.'

      do c = rng.uniform(0x7f - 0x20) + 0x20;
      while (c == 0x5c || c == 0x2f || c == 0x2e); // '\\', '/', '.'

      do d = rng.uniform(0x7f - 0x20) + 0x20;
      while (d == 0x5c || d == 0x2f || d == 0x2e); // '\\', '/', '.'

      x[0] = a;
      x[2] = b;
      y[0] = c;
      y[2] = d;

      if (a < c || (a == c && b < d))
        test_path_less_than(x, y);
      else if (a > c || (a == c && b > d))
        test_path_less_than(y, x);
      else
        test_path_equal(x, y);
    }
}

UNIT_TEST(test_internal_string_is_bookkeeping_path)
{
  char const * const yes[] = {"_MTN",
                        "_MTN/foo",
                        "_mtn/Foo",
                        0 };
  char const * const no[] = {"foo/_MTN",
                       "foo/bar",
                       0 };
  for (char const * const * c = yes; *c; ++c)
    UNIT_TEST_CHECK(bookkeeping_path
                    ::internal_string_is_bookkeeping_path(utf8(std::string(*c),
                                                               origin::internal)));
  for (char const * const * c = no; *c; ++c)
    UNIT_TEST_CHECK(!bookkeeping_path
                    ::internal_string_is_bookkeeping_path(utf8(std::string(*c),
                                                               origin::internal)));
}

UNIT_TEST(test_external_string_is_bookkeeping_path_prefix_none)
{
  initial_rel_path.unset();
  initial_rel_path.set(string(), true);

  char const * const yes[] = {"_MTN",
                        "_MTN/foo",
                        "_mtn/Foo",
                        "_MTN/foo/..",
                        0 };
  char const * const no[] = {"foo/_MTN",
                       "foo/bar",
                       "_MTN/..",
                       0 };
  for (char const * const * c = yes; *c; ++c)
    UNIT_TEST_CHECK(bookkeeping_path
                    ::external_string_is_bookkeeping_path(utf8(std::string(*c),
                                                               origin::internal)));
  for (char const * const * c = no; *c; ++c)
    UNIT_TEST_CHECK(!bookkeeping_path
                    ::external_string_is_bookkeeping_path(utf8(std::string(*c),
                                                               origin::internal)));
}

UNIT_TEST(test_external_string_is_bookkeeping_path_prefix_a_b)
{
  initial_rel_path.unset();
  initial_rel_path.set(string("a/b"), true);

  char const * const yes[] = {"../../_MTN",
                        "../../_MTN/foo",
                        "../../_mtn/Foo",
                        "../../_MTN/foo/..",
                        "../../foo/../_MTN/foo",
                        0 };
  char const * const no[] = {"foo/_MTN",
                       "foo/bar",
                       "_MTN",
                       "../../foo/_MTN",
                       0 };
  for (char const * const * c = yes; *c; ++c)
    UNIT_TEST_CHECK(bookkeeping_path
                    ::external_string_is_bookkeeping_path(utf8(std::string(*c),
                                                               origin::internal)));
  for (char const * const * c = no; *c; ++c)
    UNIT_TEST_CHECK(!bookkeeping_path
                    ::external_string_is_bookkeeping_path(utf8(std::string(*c),
                                                               origin::internal)));
}

UNIT_TEST(test_external_string_is_bookkeeping_path_prefix__MTN)
{
  initial_rel_path.unset();
  initial_rel_path.set(string("_MTN"), true);

  char const * const yes[] = {".",
                        "foo",
                        "../_MTN/foo/..",
                        "../_mtn/foo",
                        "../foo/../_MTN/foo",
                        0 };
  char const * const no[] = {"../foo",
                       "../foo/bar",
                       "../foo/_MTN",
#ifdef WIN32
                       "c:/foo/foo", // don't throw informative_failure exception
#else
                       "/foo/foo", // don't throw informative_failure exception
#endif
                       0 };
  for (char const * const * c = yes; *c; ++c)
    UNIT_TEST_CHECK(bookkeeping_path
                    ::external_string_is_bookkeeping_path(utf8(std::string(*c),
                                                               origin::internal)));
  for (char const * const * c = no; *c; ++c)
    UNIT_TEST_CHECK(!bookkeeping_path
                    ::external_string_is_bookkeeping_path(utf8(std::string(*c),
                                                               origin::internal)));
}

UNIT_TEST(find_old_new_path_for)
{
  map<file_path, file_path> renames;
  file_path foo = file_path_internal("foo");
  file_path foo_bar = file_path_internal("foo/bar");
  file_path foo_baz = file_path_internal("foo/baz");
  file_path quux = file_path_internal("quux");
  file_path quux_baz = file_path_internal("quux/baz");
  I(foo == find_old_path_for(renames, foo));
  I(foo == find_new_path_for(renames, foo));
  I(foo_bar == find_old_path_for(renames, foo_bar));
  I(foo_bar == find_new_path_for(renames, foo_bar));
  I(quux == find_old_path_for(renames, quux));
  I(quux == find_new_path_for(renames, quux));
  renames.insert(make_pair(foo, quux));
  renames.insert(make_pair(foo_bar, foo_baz));
  I(quux == find_old_path_for(renames, foo));
  I(foo == find_new_path_for(renames, quux));
  I(quux_baz == find_old_path_for(renames, foo_baz));
  I(foo_baz == find_new_path_for(renames, quux_baz));
  I(foo_baz == find_old_path_for(renames, foo_bar));
  I(foo_bar == find_new_path_for(renames, foo_baz));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
