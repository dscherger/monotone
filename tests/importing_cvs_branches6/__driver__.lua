
mtn_setup()

-- Manually created and hand edited CVS repository.
--
-- Branch layout overview:
--
--     Root
--     / \
--    A   B
--
-- The commits in branches A and B both include file1. They have the
-- same timestamp, changelog and author.

check(get("cvs-repository"))

writefile("file1-1.1", "version 1.1 of test file1\n")
writefile("file1-1.1.2.1", "version 1.1.2.1 of test file1\n")
writefile("file1-1.1.4.1", "version 1.1.4.1 of test file1\n")

writefile("file2-1.1", "version 1.1 of test file2\n")
writefile("file2-1.1.2.1", "version 1.1.2.1 of test file2\n")

-- import into monotone and check presence of files
xfail(mtn("--branch=test", "cvs_import", "--debug", "cvs-repository/test"), 0, false, false)


-- check if all non-empty branches were imported
check(mtn("list", "branches"), 0, true, false)
check(samelines("stdout", {"test", "test.A", "test.B"}))

-- checkout the imported repository into maindir and branchdir
check(mtn("checkout", "--branch=test", "maindir"), 0, false, false)
check(mtn("checkout", "--branch=test.A", "branchA"), 0, false, false)
check(mtn("checkout", "--branch=test.B", "branchB"), 0, false, false)

-- check for correctness of the files in the main tree
check(samefile("file1-1.1", "maindir/file1"))
check(samefile("file2-1.1", "maindir/file2"))

-- check for correctness of the files in branch A
check(samefile("file1-1.1.2.1", "branchA/file1"))
check(samefile("file2-1.1.2.1", "branchA/file2"))

-- check for correctness of the files in branch B
check(samefile("file1-1.1.4.1", "branchB/file1"))
check(samefile("file2-1.1", "branchB/file2"))

