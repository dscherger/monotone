
include("/common/cvs.lua")
mtn_setup()

writefile("importme.3", "version 3 of test file")


-- build the cvs repository

cvs_setup()

-- check out the workspace and make some commits
-- note that this has to use copy, rather than rename, to update
-- the file in cvs. Apparently, cvs uses timestamps or something to track
-- file modifications.
check(cvs("co", "."), 0, false, false)
mkdir("testdir")
writefile("testdir/importme", "version 0 of test file")
tsha0 = sha1("testdir/importme")
check(cvs("add", "testdir"), 0, false, false)
check(cvs("add", "testdir/importme"), 0, false, false)
check(cvs("commit", "-m", 'commit 0', "testdir/importme"), 0, false, false)
writefile("testdir/importme", "version 1 of test file")
tsha1 = sha1("testdir/importme")
check(cvs("commit", "-m", 'commit 1', "testdir/importme"), 0, false, false)
writefile("testdir/importme", "version 2 of test file")
tsha2 = sha1("testdir/importme")
check(cvs("commit", "-m", 'commit 2', "testdir/importme"), 0, false, false)
writefile("testdir/importme", "version 3 of test file")
tsha3 = sha1("testdir/importme")
xfail(cvs("commit", "-m", 'commit 3', "testdir/importme"), 0, false, false)

-- import into monotone using dryrun and check if the database has really
-- not been touched

check(mtn("--dry-run", "--branch=testbranch", "cvs_import", cvsroot .. "/testdir"), 0, false, false)
check(mtn("automate", "get_file", tsha0), 1, false, false)
check(mtn("automate", "get_file", tsha1), 1, false, false)
check(mtn("automate", "get_file", tsha2), 1, false, false)
check(mtn("automate", "get_file", tsha3), 1, false, false)

-- also check that history is okay -- has a unique head, and it's the
-- right one.

check(mtn("checkout", "--branch=testbranch", "mtcodir"), 1, false, false)
