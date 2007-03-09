
mtn_setup()

-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

writefile("foo.0", "version 0 of test file foo\n")
writefile("foo.1", "version 1 of test file foo\n")
writefile("foo.2", "version 2 of test file foo\n")
writefile("foo.3", "version 3 of test file foo\n")
tsha0 = sha1("foo.0")
tsha1 = sha1("foo.1")
tsha2 = sha1("foo.2")
tsha3 = sha1("foo.3")

-- import into monotone using dryrun and check if the database has really
-- not been touched

check(mtn("--dry-run", "--branch=testbranch", "cvs_import", "cvs-repository/test"), 0, false, false)
check(mtn("automate", "get_file", tsha0), 1, false, false)
check(mtn("automate", "get_file", tsha1), 1, false, false)
check(mtn("automate", "get_file", tsha2), 1, false, false)
check(mtn("automate", "get_file", tsha3), 1, false, false)

-- try to check out the testbranch, which should fail, too.
check(mtn("checkout", "--branch=testbranch", "mtcodir"), 1, false, false)
