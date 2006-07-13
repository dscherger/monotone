mtn_setup()

-- This test is designed to tickle some bugs in the restrictions code.  In
-- particular, we want to prevent the case where the addition of foo/bar is
-- included in a cset, but the addition of foo/ is not -- that would result in
-- a nonsense cset.  However, the current code only knows about adds, not
-- about deletes or renames.

-- ways to need a path: add, rename
-- ways to lose a path: drop, rename

addfile("whatever", "balsdfas")
commit()
root_rev = base_revision()

-- easiest way to get: rename a dir, and add something to it, and then diff
revert_to(root_rev)
mkdir("testdir")
check(mtn("add", "testdir"), 0, false, false)
commit()
check(mtn("mv", "-e", "testdir", "newdir"), 0, false, false)
addfile("newdir/foo", "blah blah\n")
-- these should succeed, but they error out.
xfail(mtn("diff"), 0, false, false)
xfail(mtn("commit", "-m", "foo"), 0, false, false)

-- or: rename a dir A, add a replacement B, add something C to the
--   replacement, then use a restriction that includes A and C only
revert_to(root_rev)
mkdir("testdir")
check(mtn("add", "testdir"), 0, false, false)
commit()
check(mtn("mv", "-e", "testdir", "newdir"), 0, false, false)
mkdir("testdir")
addfile("testdir/newfile", "asdfasdf")
-- these are nonsensical, but instead of erroring out gracefully, diff
-- succeeds and commit asserts out in roster.cc
xfail(mtn("diff", "newdir", "testdir/newfile"), 1, false, false)
xfail(mtn("commit", "newdir", "testdir/newfile"), 1, false, false)

-- or: rename A, then rename B under it, and use a restriction that includes
--   only B
revert_to(root_rev)
mkdir("A")
mkdir("B")
check(mtn("add", "A", "B"), 0, false, false)
commit()
check(mtn("rename", "-e", "A", "newA"), 0, false, false)
check(mtn("rename", "-e", "B", "newA/B"), 0, false, false)
-- these are nonsensical, but instead of erroring out gracefully, diff
-- succeeds and commit asserts out in roster.cc
xfail(mtn("diff", "newA/B"), 1, false, false)
xfail(mtn("commit", "newA/B"), 1, false, false)
