
include("/common/cvs.lua")
mtn_setup()
cvs_setup()

-- A test case from Michael Haggerty, author of cvs2svn
-- http://cvs2svn.tigris.org/servlets/ReadMsg?list=dev&msgNo=1878
--
-- He gave a very detailed description in the above email. This
-- adoption for monotone automatically checks the result, but is
-- otherwise equivalent.

check(cvs("co", "."), 0, false, false)
mkdir("testdir")
check(cvs("add", "testdir"), 0, false, false)
check(cvs("commit", "-m", "add dir"), 0, false, false)

writefile("testdir/fileA", "1.1")
check(cvs("add", "testdir/fileA"), 0, false, false)

writefile("testdir/fileB", "1.1")
check(cvs("add", "testdir/fileB"), 0, false, false)

writefile("testdir/fileC", "1.1")
check(cvs("add", "testdir/fileC"), 0, false, false)

writefile("testdir/fileD", "1.1")
check(cvs("add", "testdir/fileD"), 0, false, false)

check(cvs("commit", "-m", "initial versions"), 0, false, false)

check(cvs("tag", "-b", "A1"), 0, false, false)

writefile("testdir/fileD", "1.2")
check(cvs("commit", "-m", "revision 1.2 of fileD"), 0, false, false)

check(cvs("tag", "-b", "A2"), 0, false, false)

-- switch to branch "A1"
check(cvs("update", "-r", "A1"), 0, false, false)

writefile("testdir/fileB", "1.1.2.1")
check(cvs("commit", "-m", "revision 1.1.2.1 of fileB"), 0, false, false)

-- create branch "B" and switch to branch:
check(cvs("tag", "-b", "B"), 0, false, false)
check(cvs("update", "-r", "B"), 0, false, false)

writefile("testdir/fileC", "1.1.6.1")
check(cvs("commit", "-m", "revision 1.1.6.1 of fileC"), 0, false, false)

check(cvs("tag", "X"), 0, false, false)

-- import and check
check(mtn("--branch=test", "cvs_import", cvsroot.."/testdir"), 0, false, false)

check(mtn("list", "branches"), 0, true, false)
check(samelines("stdout", {"test", "test.A1", "test.B"}))

check(mtn("checkout", "-b", "test.A1", "mtnco"), 0, false, false)
check(indir("mtnco", mtn("list", "known")), 0, true, false)
check(samelines("stdout", {"fileA", "fileB", "fileC", "fileD"}))
remove("mtnco")

check(mtn("checkout", "-r", "X", "mtnco"), 0, false, false)
check(indir("mtnco", mtn("list", "known")), 0, true, false)
check(samelines("stdout", {"fileA", "fileB", "fileC", "fileD"}))
check(samefile("mtnco/fileA", "testdir/fileA"))
check(samefile("mtnco/fileB", "testdir/fileB"))
check(samefile("mtnco/fileC", "testdir/fileC"))
check(samefile("mtnco/fileD", "testdir/fileD"))
