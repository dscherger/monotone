-- most simple CVS push 

include("/common/cvs.lua")
mtn_setup()
cvs_setup()

-- create a test project

mkdir("cvstemp")
writefile("cvstemp/A","A\n")
check(indir("cvstemp", cvs("import","-m","initial import", "test", "vendor_tag", "initial_import")), 0, false, false)

-- change the repository in various ways
check(mtn_cvs("--branch=testbranch","pull",cvsroot,"test"),0, false,false)
check(mtn("co","--branch=testbranch","testbr"),0, false, false)

writefile("testbr/A", "new A\n")
tsha1 = sha1("testbr/A")
check(indir("testbr", mtn("ci", "-m", "a changed")), 0, false, false)

check(mtn_cvs("--branch=testbranch","push"),0, false,false)

check(cvs("co", "test"), 0, false, false)
check(sha1("test/A") == tsha1)
