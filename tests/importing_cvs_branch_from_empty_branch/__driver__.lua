
mtn_setup()


-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

-- A test case from Michael Haggerty, author of cvs2svn
-- http://cvs2svn.tigris.org/servlets/ReadMsg?list=dev&msgNo=1878
--
-- He gave a very detailed description in the above email. This
-- adoption for monotone automatically checks the result, but is
-- otherwise equivalent.


-- import and check
check(mtn("--branch=test", "cvs_import", "cvs-repository/test"), 0, false, false)

check(mtn("list", "branches"), 0, true, false)
check(samelines("stdout", {"test", "test.A1", "test.B"}))

check(mtn("checkout", "-b", "test.A1", "mtnco"), 0, false, false)
check(indir("mtnco", mtn("list", "known")), 0, true, false)
check(samelines("stdout", {"fileA", "fileB", "fileC", "fileD"}))
remove("mtnco")

-- checkout tag X and check its contents
check(mtn("checkout", "-r", "X", "mtnco"), 0, false, false)
check(indir("mtnco", mtn("list", "known")), 0, true, false)
check(samelines("stdout", {"fileA", "fileB", "fileC", "fileD"}))
check(samelines("mtnco/fileA", {"1.1"}))
check(samelines("mtnco/fileB", {"1.1.2.1"}))
check(samelines("mtnco/fileC", {"1.1.6.1"}))
check(samelines("mtnco/fileD", {"1.1"}))
