include("common/cvs.lua")
mtn_setup()

--
-- Branch layout overview:
--
--  Root
--    |
--    A
--    |\
--    | C -- D
--    |
--    B
--
-- Root contains file1, file2 and changelog, branch C adds a file3, but does not
-- touch any other file. D then updates all files. No commits on branch B, it
-- remains empty.

writefile("file1.0", "version 0 of test file1")
writefile("file1.1", "version 1 of test file1")
writefile("file1.2", "version 2 of test file1")
writefile("file1.3", "version 3 of test file1")
writefile("file2.0", "version 0 of test file2")
writefile("file2.1", "version 1 of test file2")
writefile("file2.2", "version 2 of test file2")
writefile("file3.0", "version 0 of test file3")
writefile("file3.1", "version 1 of test file3")
writefile("changelog.0", "first changelog entry")
writefile("changelog.1", "second changelog\n\n"..readfile("changelog.0"))
writefile("changelog.2", "third changelog -not on branch-\n\n"..readfile("changelog.1"))
writefile("changelog.3", "third changelog -on branch A-\n\n"..readfile("changelog.1"))
writefile("changelog.4", "fourth changelog -on branch C-\n\n"..readfile("changelog.3"))
writefile("changelog.5", "fifth changelog -on branch D-\n\n"..readfile("changelog.4"))

-- build the cvs repository

cvs_setup()

-- checkout the empty repository and commit some files

check(cvs("co", "."), 0, false, false)
mkdir("testdir")
copy("file1.0", "testdir/file1")
copy("file2.0", "testdir/file2")
copy("changelog.0", "testdir/changelog")
check(cvs("add", "testdir"), 0, false, false)
check(cvs("add", "testdir/file1"), 0, false, false)
check(cvs("add", "testdir/file2"), 0, false, false)
check(cvs("add", "testdir/changelog"), 0, false, false)
check(cvs("commit", "-m", 'initial import', "testdir/file1", "testdir/file2", "testdir/changelog"), 0, false, false)

-- commit first changes
copy("file1.1", "testdir/file1")
copy("changelog.1", "testdir/changelog")
check(cvs("commit", "-m", 'first commit', "testdir/file1", "testdir/changelog"), 0, false, false)

-- now we create a branch A
check(indir("testdir", cvs("tag", "-b", "A")), 0, false, false)
check(indir("testdir", cvs("up", "-r", "A")), 0, false, false)

-- alter the files on branch A
copy("file2.1", "testdir/file2")
copy("changelog.3", "testdir/changelog")
check(cvs("commit", "-m", 'commit on branch A', "testdir/file2", "testdir/changelog"), 0, false, false)

-- branch again into B
check(indir("testdir", cvs("tag", "-b", "B")), 0, false, false)
check(indir("testdir", cvs("up", "-r", "B")), 0, false, false)
-- branch B is left untouched

-- go back to A and branch into C
check(cvs("up", "-r", "A", "-A"), 0, false, false)
check(indir("testdir", cvs("tag", "-b", "C")), 0, false, false)
check(indir("testdir", cvs("up", "-r", "C")), 0, false, false)

-- add a file3
copy("file3.0", "testdir/file3")
copy("changelog.4", "testdir/changelog")
check(indir("testdir", cvs("add", "file3")), 0, false, false)
check(indir("testdir", cvs("commit", "-m", 'commit on branch C', "file3", "changelog")), 0, false, false)

-- branch into D
check(indir("testdir", cvs("tag", "-b", "D")), 0, false, false)
check(indir("testdir", cvs("up", "-r", "D")), 0, false, false)
copy("file1.3", "testdir/file1")
copy("file2.2", "testdir/file2")
copy("file3.1", "testdir/file3")
copy("changelog.5", "testdir/changelog")
check(indir("testdir", cvs("commit", "-m", 'commit on branch D', "file1", "file2", "file3", "changelog")), 0, false, false)

-- and create some mainline changes after the branch
check(cvs("up", "-A"), 0, false, false)
copy("file1.2", "testdir/file1")
copy("changelog.2", "testdir/changelog")
check(cvs("commit", "-m", 'commit on mainline after branch', "testdir/file1", "testdir/changelog"), 0, false, false)

-- import into monotone and check presence of files
check(mtn("--branch=test", "cvs_import", cvsroot.."/testdir"), 0, false, false)

-- check if all branches were imported
check(mtn("list", "branches"), 0, true, false)
check(samelines("stdout", {"test", "test.A", "test.B", "test.C", "test.D"}))

-- checkout the imported repository into maindir and branchdir
check(mtn("checkout", "--branch=test", "maindir"), 0, false, false)
check(mtn("checkout", "--branch=test.A", "branchA"), 0, false, false)
check(mtn("checkout", "--branch=test.B", "branchB"), 0, false, false)
check(mtn("checkout", "--branch=test.C", "branchC"), 0, false, false)
check(mtn("checkout", "--branch=test.D", "branchD"), 0, false, false)

-- check for correctness of the files in the main tree
check(samefile("file1.2", "maindir/file1"))
check(samefile("file2.0", "maindir/file2"))
check(samefile("changelog.2", "maindir/changelog"))

-- check for correctness of the files in branch A
check(samefile("file1.1", "branchA/file1"))
check(samefile("file2.1", "branchA/file2"))
check(samefile("changelog.3", "branchA/changelog"))

-- check for correctness of the files in branch B
check(samefile("file1.1", "branchB/file1"))
check(samefile("file2.1", "branchB/file2"))
check(samefile("changelog.3", "branchB/changelog"))

-- check for correctness of the files in branch C
check(samefile("file1.1", "branchC/file1"))
check(samefile("file2.1", "branchC/file2"))
check(samefile("file3.0", "branchC/file3"))
check(samefile("changelog.4", "branchC/changelog"))

-- check for correctness of the files in branch D
check(samefile("file1.3", "branchD/file1"))
check(samefile("file2.2", "branchD/file2"))
check(samefile("file3.1", "branchD/file3"))
check(samefile("changelog.5", "branchD/changelog"))

-- check the log of branch A for correctness
check(indir("branchA", mtn("log")), 0, true, false)
check(grep("beginning of branch", "stdout"), 1, false, false)
check(grep("initial import", "stdout"), 0, false, false)
check(grep("first commit", "stdout"), 0, false, false)
check(grep("commit on branch A", "stdout"), 0, false, false)
check(grep("commit on branch B", "stdout"), 1, false, false)
check(grep("commit on branch C", "stdout"), 1, false, false)
check(grep("commit on branch D", "stdout"), 1, false, false)

-- check the log of branch B for correctness
check(indir("branchB", mtn("log")), 0, true, false)
check(grep("initial import", "stdout"), 0, false, false)
check(grep("first commit", "stdout"), 0, false, false)
check(grep("commit on branch A", "stdout"), 0, false, false)
check(grep("beginning of branch test.B", "stdout"), 0, false, false)
check(grep("commit on branch C", "stdout"), 1, false, false)
check(grep("commit on branch D", "stdout"), 1, false, false)

-- check the log of branch C for correctness
check(indir("branchC", mtn("log")), 0, true, false)
check(grep("beginning of branch", "stdout"), 1, false, false)
check(grep("initial import", "stdout"), 0, false, false)
check(grep("first commit", "stdout"), 0, false, false)
check(grep("commit on branch A", "stdout"), 0, false, false)
check(grep("commit on branch B", "stdout"), 1, false, false)
check(grep("commit on branch C", "stdout"), 0, false, false)
check(grep("commit on branch D", "stdout"), 1, false, false)

-- check the log of branch D for correctness
check(indir("branchD", mtn("log")), 0, true, false)
check(grep("beginning of branch", "stdout"), 1, false, false)
check(grep("initial import", "stdout"), 0, false, false)
check(grep("first commit", "stdout"), 0, false, false)
xfail(grep("commit on branch A", "stdout"), 0, false, false)
xfail(grep("commit on branch B", "stdout"), 1, false, false)
xfail(grep("commit on branch C", "stdout"), 0, false, false)
xfail(grep("commit on branch D", "stdout"), 0, false, false)

