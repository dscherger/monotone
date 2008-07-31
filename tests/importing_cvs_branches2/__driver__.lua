
mtn_setup()


-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

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

writefile("file1-1.1", "version 1.1 of test file1\n")
writefile("file1-1.2", "version 1.2 of test file1\n")
writefile("file1-1.3", "version 1.3 of test file1\n")
writefile("file1-1.2.8.1", "version 1.2.8.1 of test file1\n")
writefile("file2-1.1", "version 1.1 of test file2\n")
writefile("file2-1.1.2.1", "version 1.1.2.1 of test file2\n")
writefile("file2-1.1.2.1.6.1", "version 1.1.2.1.6.1 of test file2\n")
writefile("file3-1.1.2.1", "version 1.1.2.1 of test file3\n")
writefile("file3-1.1.2.1.2.1", "version 1.1.2.1.2.1 of test file3\n")

writefile("changelog.1", "first changelog entry\n")
writefile("changelog.2", readfile("changelog.1").."second changelog entry\n")
writefile("changelog.3", readfile("changelog.2").."third changelog -not on branch-\n")
writefile("changelog.A.3", readfile("changelog.2").."third changelog -on branch A-\n")
writefile("changelog.C.4", readfile("changelog.A.3").."fourth changelog -on branch C-\n")
writefile("changelog.D.5", readfile("changelog.C.4").."fifth changelog -on branch D-\n")

-- import into monotone and check presence of files
check(mtn("--branch=test", "cvs_import", "cvs-repository/test"), 0, false, false)

-- check if all non-empty branches were imported
check(mtn("list", "branches"), 0, true, false)
check(samelines("stdout", {"test", "test.A", "test.C", "test.D"}))

-- checkout the imported repository into maindir and branchdir
check(mtn("checkout", "--branch=test", "maindir"), 0, false, false)
check(mtn("checkout", "--branch=test.A", "branchA"), 0, false, false)
check(mtn("checkout", "--branch=test.C", "branchC"), 0, false, false)
check(mtn("checkout", "--branch=test.D", "branchD"), 0, false, false)

-- check for correctness of the files in the main tree
check(samefile("file1-1.3", "maindir/file1"))
check(samefile("file2-1.1", "maindir/file2"))
check(samefile("changelog.3", "maindir/changelog"))

-- check for correctness of the files in branch A
check(samefile("file1-1.2", "branchA/file1"))
check(samefile("file2-1.1.2.1", "branchA/file2"))
check(samefile("changelog.A.3", "branchA/changelog"))

-- check for correctness of the files in branch C
check(samefile("file1-1.2", "branchC/file1"))
check(samefile("file2-1.1.2.1", "branchC/file2"))
check(samefile("file3-1.1.2.1", "branchC/file3"))
check(samefile("changelog.C.4", "branchC/changelog"))

-- check for correctness of the files in branch D
check(samefile("file1-1.2.8.1", "branchD/file1"))
check(samefile("file2-1.1.2.1.6.1", "branchD/file2"))
check(samefile("file3-1.1.2.1.2.1", "branchD/file3"))
check(samefile("changelog.D.5", "branchD/changelog"))

-- check the log of branch A for correctness
check(indir("branchA", mtn("log", "--no-graph")), 0, true, false)
check(grep("initial import", "stdout"), 0, false, false)
check(grep("first commit", "stdout"), 0, false, false)
check(grep("commit on branch A", "stdout"), 0, false, false)
check(grep("commit on branch B", "stdout"), 1, false, false)
check(grep("commit on branch C", "stdout"), 1, false, false)
check(grep("commit on branch D", "stdout"), 1, false, false)

-- check the log of branch C for correctness
check(indir("branchC", mtn("log", "--no-graph")), 0, true, false)
check(grep("initial import", "stdout"), 0, false, false)
check(grep("first commit", "stdout"), 0, false, false)
check(grep("commit on branch A", "stdout"), 0, false, false)
check(grep("commit on branch B", "stdout"), 1, false, false)
check(grep("commit on branch C", "stdout"), 0, false, false)
check(grep("commit on branch D", "stdout"), 1, false, false)

-- check the log of branch D for correctness
check(indir("branchD", mtn("log", "--no-graph")), 0, true, false)
check(grep("initial import", "stdout"), 0, false, false)
check(grep("first commit", "stdout"), 0, false, false)
check(grep("commit on branch A", "stdout"), 0, false, false)
check(grep("commit on branch B", "stdout"), 1, false, false)
check(grep("commit on branch C", "stdout"), 0, false, false)
check(grep("commit on branch D", "stdout"), 0, false, false)

