
mtn_setup()

-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

-- Opposed to the first version of this test, here we have commits in
-- between the two commits with the same changelogs. This means, we
-- internally end up with a cycle consisting of multiple blobs.

writefile("foo.0", "version 0 of test file foo\n")
writefile("foo.1", "version 1 of test file foo\n")
writefile("foo.2", "version 2 of test file foo\n")
writefile("foo.3", "version 3 of test file foo\n")
tsha0 = sha1("foo.0")
tsha1 = sha1("foo.1")
tsha2 = sha1("foo.2")
tsha3 = sha1("foo.3")

-- import into monotone and check presence of files
check(mtn("--branch=testbranch", "cvs_import", "cvs-repository/test"), 0, false, false)
check(mtn("automate", "get_file", tsha0), 0, false)
check(mtn("automate", "get_file", tsha1), 0, false)
check(mtn("automate", "get_file", tsha2), 0, false)
check(mtn("automate", "get_file", tsha3), 0, false)

-- also check that history is okay -- has a unique head, and it's the
-- right one.
check(mtn("checkout", "--branch=testbranch", "mtcodir"), 0, false, false)
check(samefile("foo.3", "mtcodir/foo"))
