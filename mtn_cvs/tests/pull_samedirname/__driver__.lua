
include("/common/cvs.lua")
mtn_setup()
cvs_setup()

check(cvs("co","."), 0, false, false)
mkdir("testdir")
writefile("testdir/importme","version 0 of test file\n")
tsha0=sha1("testdir/importme")
check(cvs("add","testdir"),0,false,false)
check(cvs("add","testdir/importme"),0,false,false)
check(cvs("commit","-m","commit 0","testdir/importme"),0,false,false)
writefile("testdir/importme","version 1 of test file\n")
tsha1=sha1("testdir/importme")
check(cvs("commit","-m","commit 1","testdir/importme"),0,false,false)
writefile("testdir/importme","version 2 of test file\n")
tsha2=sha1("testdir/importme")
check(cvs("commit","-m","commit 2","testdir/importme"),0,false,false)
mkdir("testdir/testdir")
writefile("testdir/testdir/importme","version 3 of test file\n")
tsha3=sha1("testdir/testdir/importme")
check(cvs("add","testdir/testdir"),0,false,false)
check(cvs("add","testdir/testdir/importme"),0,false,false)
check(cvs("commit","-m","commit 3","testdir/testdir/importme"),0,false,false)

-- pull into monotone

check(mtn_cvs("--branch=testbranch","pull",cvsroot,"testdir"),0,false,false)

-- check presence of files

check(mtn("automate", "get_file", tsha0), 0, false)
check(mtn("automate", "get_file", tsha1), 0, false)
check(mtn("automate", "get_file", tsha2), 0, false)
check(mtn("automate", "get_file", tsha3), 0, false)

-- also check that history is okay -- has a unique head, and it's the
-- right one.

check(mtn("checkout", "--branch=testbranch", "mtcodir"), 0, false, false)
check(sha1("mtcodir/importme") == tsha2)
check(sha1("mtcodir/testdir/importme") == tsha3)
