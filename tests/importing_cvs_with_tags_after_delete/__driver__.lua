
include("/common/cvs.lua")
mtn_setup()
cvs_setup()

-- A test case inspired by Marko Macek from the dev@cvs2svn mailing list:
-- http://cvs2svn.tigris.org/servlets/ReadMsg?list=dev&msgNo=1877
--
-- It creates two files, foo and bar, tags then deletes bar and tags
-- again. The monotone repository is then checked for correct files
-- in the tagged revisions.

check(cvs("co", "."), 0, false, false)
mkdir("testdir")
writefile("testdir/foo", "foo")
writefile("testdir/bar", "bar")
check(cvs("add", "testdir"), 0, false, false)
check(cvs("add", "testdir/foo"), 0, false, false)
check(cvs("add", "testdir/bar"), 0, false, false)
check(cvs("commit", "-m", 'Initial import'), 0, false, false)

-- check out the repository created and tag it
check(cvs("tag", "FOO_AND_BAR"), 0, false, false)

-- delete bar and tag again
check(indir("testdir", cvs("remove", "-f", "bar")), 0, false, false)
check(cvs("commit", "-m", "removed bar"), 0, false, false)
check(cvs("tag", "FOO_ONLY"), 0, false, false)

-- import and check
check(mtn("--branch=test", "cvs_import", cvsroot.."/testdir"), 0, false, false)

check(mtn("checkout", "--revision=FOO_AND_BAR", "mtnco"), 0, false, false)
check(indir("mtnco", mtn("list", "known")), 0, true, false)
check(samelines("stdout", {"bar", "foo"}))
remove("mtnco")

check(mtn("checkout", "--revision=FOO_ONLY", "mtnco"), 0, false, false)
check(indir("mtnco", mtn("list", "known")), 0, true, false)
check(samelines("stdout", {"foo"}))
remove("mtnco")
