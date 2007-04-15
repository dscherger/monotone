-- test pushing of a rename to CVS

include("/common/cvs.lua")
mtn_setup()
cvs_setup()

-- create a test project

mkdir("cvstemp")
writefile("cvstemp/A","A\n")
check(indir("cvstemp", cvs("import","-m","initial import", "test", "vendor_tag", "initial_import")), 0, false, false)

-- get the mtn version
check(mtn_cvs("--branch=testbranch", "pull", cvsroot, "test"), 0, false, false)
check(mtn("co","--branch=testbranch","testbr"),0, false, false)

-- change the mtn working copy
writefile("testbr/A","Changed A\n")
tsha1 = sha1("testbr/A")
check(indir("testbr", mtn("rename", "A", "B")), 0, false, false)

-- commit the mtn working copy
check(indir("testbr", mtn("ci", "-m", "A renamed to B")), 0, false, false)

-- push the mtn working copy back to cvs
check(mtn_cvs("--branch=testbranch", "push"), 0, false, true)

-- update the cvs working copy and check that the change made it ok
check(cvs("co", "test"), 0, false, false)
check(exists("test/B"))
check(not exists("test/A"))
check(sha1("test/B") == tsha1)

-- make some further changes on the cvs side and check that they're ok
writefile("test/B","Changed in CVS\n")
tsha2 = sha1("test/B")
check(indir("test", cvs("ci", "-m", "a log message")), 0, false, false)
check(mtn_cvs("--branch=testbranch", "pull", cvsroot, "test"), 0, false, false)
check(indir("testbr", mtn("up")),0, false, false)
check(sha1("testbr/B") == tsha2)

-- and now check that further changes on the mtn side make it over ok
writefile("testbr/B","Yet another change\n")
tsha3 = sha1("testbr/B")
check(indir("testbr", mtn("ci", "-m", "a change in mtn")), 0, false, false)
check(mtn_cvs("--branch=testbranch", "push"), 0, false, false)
check(indir("test", cvs("up")), 0, false, false)
check(sha1("test/B") == tsha3)
