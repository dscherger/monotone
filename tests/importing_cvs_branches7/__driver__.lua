
mtn_setup()


-- There's a makerepo.sh as well, but the repository for this test has
-- been edited as follows: the timestamp from 1.2 is being copied to
-- revisions 1.1.2.2, 1.1.4.2 and 1.1.6.2 to trigger the following
-- nasty effect.
check(get("cvs-repository"))

--
-- Branch layout overview:
--
--      ROOT
--     / | \
--    A  B  C
--
-- Root contains file1. All branches are created and file1 gets a first
-- modification. Then a patch - often some sort of a backport - gets
-- applied to the ROOT, but at the same time also to all branches. That
-- easily ends up with multiple commits having the same timestamp, author
-- and changelog.

writefile("file1-1.2", "version 1.2 of test file1\n")
writefile("file1-1.1.2.2", "version 1.1.2.2 of test file1\n")
writefile("file1-1.1.4.2", "version 1.1.4.2 of test file1\n")
writefile("file1-1.1.6.2", "version 1.1.6.2 of test file1\n")

-- import into monotone and check presence of files
check(mtn("--branch=test", "cvs_import", "cvs-repository/test", "--debug"), 0, false, false)

-- check if all non-empty branches were imported
check(mtn("list", "branches"), 0, true, false)
check(samelines("stdout", {"test", "test.A", "test.B", "test.C"}))

-- checkout the imported repository into maindir and branchdir
check(mtn("checkout", "--branch=test", "maindir"), 0, false, false)
check(mtn("checkout", "--branch=test.A", "branchA"), 0, false, false)
check(mtn("checkout", "--branch=test.B", "branchB"), 0, false, false)
check(mtn("checkout", "--branch=test.C", "branchC"), 0, false, false)

-- check for correct contents of the file in the main tree
check(samefile("file1-1.2", "maindir/file1"))

-- check for correct contents of the file in branch A
check(samefile("file1-1.1.2.2", "branchA/file1"))

-- check for correct contents of the file in branch B
check(samefile("file1-1.1.4.2", "branchB/file1"))

-- check for correct contents of the file in branch C
check(samefile("file1-1.1.6.2", "branchC/file1"))

