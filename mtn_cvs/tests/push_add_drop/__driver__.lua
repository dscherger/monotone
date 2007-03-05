-- Test pushing of add and drop to CVS

include("/common/cvs.lua")
mtn_setup()
cvs_setup()

-- create a test project

mkdir("cvstemp")
writefile("cvstemp/A","A\n")
check(indir("cvstemp", cvs("import","-m","initial import", "test", "vendor_tag", "initial_import")), 0, false, false)

-- get the mtn version
check(mtn_cvs("--branch=testbranch","pull",cvsroot,"test"),0, false,false)
check(mtn("co","--branch=testbranch","testbr"),0, false, false)

-- change the mtn version
check(indir("testbr", mtn("drop", "A")), 0, false, false)
writefile("testbr/B","B\n")
check(indir("testbr", mtn("add", "B")), 0, false, false)
tsha1 = sha1("testbr/B")

-- commit the mtn version
check(indir("testbr", mtn("ci", "-m", "A dropped and B added")), 0, false, false)

-- push the mtn version back to cvs
check(mtn_cvs("--branch=testbranch","push"),0, false,false)

-- update the cvs working copy and check that the change made it ok
check(cvs("co", "test"), 0, false, false)
check(exists("test/B"))
check(sha1("test/B") == tsha1)
check(not exists("test/A"))

-- Now, without any CVS changes, check if another pull pulls anything across

writefile("testbr/B","change\n")
check(indir("testbr", mtn("ci", "-m", "B changed")), 0, false, false)
check(mtn_cvs("--branch=testbranch","pull",cvsroot,"test"),0, false,false)  -- should be noop.

  -- if we pulled anything then this will fail with multiple heads
check(mtn("checkout", "--branch=testbranch", "mtcodir"), 0, false, false)
