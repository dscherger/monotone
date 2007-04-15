-- test pushing of a rename to CVS

include("/common/cvs.lua")
mtn_setup()
cvs_setup()

-- create a test project

mkdir("cvstemp")
writefile("cvstemp/A","A\n")
writefile("cvstemp/B","B\n")
check(indir("cvstemp", cvs("import","-m","initial import", "test", "vendor_tag", "initial_import")), 0, false, false)

-- get the mtn version
check(mtn_cvs("--branch=testbranch", "pull", cvsroot, "test"), 0, false, false)
check(mtn("co","--branch=testbranch","testbr"),0, false, false)

-- change the mtn working copy
tsha1 = sha1("testbr/A")
tsha2 = sha1("testbr/B")
check(indir("testbr", mtn("rename", "A", "C")), 0, false, false)
check(indir("testbr", mtn("rename", "B", "A")), 0, false, false)
check(indir("testbr", mtn("rename", "C", "B")), 0, false, false)

-- commit the mtn working copy
check(indir("testbr", mtn("ci", "-m", "A renamed to B and vice versa")), 0, false, false)

-- push the mtn working copy back to cvs
check(mtn_cvs("--branch=testbranch", "push"), 0, false, false)

-- update the cvs working copy and check that the change made it ok
check(cvs("co", "test"), 0, false, false)
check(exists("test/A"))
check(exists("test/B"))

-- the following checks fail, but the failure is actually above in the push - it just fails without a bad error code
xfail(sha1("test/A") == tsha2)
check(sha1("test/B") == tsha1)
