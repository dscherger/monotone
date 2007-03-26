-- most simple CVS push 

include("/common/cvs.lua")
mtn_setup()
cvs_setup()

-- create a test project

mkdir("cvstemp")
writefile("cvstemp/A","A\n")
writefile("cvstemp/B","B\n")
check(indir("cvstemp", cvs("import","-m","initial import", "test", "vendor_tag", "initial_import")), 0, false, false)

check(cvs("co", "test"), 0, false, false)
rename("test", "cvs-test")
writefile("cvs-test/A", "cvs A\n")
writefile("cvs-test/B", "cvs B\n")
check(indir("cvs-test", cvs("co", "test")), 0, false, false)

-- change the repository in various ways
check(mtn_cvs("--branch=testbranch","pull",cvsroot,"test"),0, false,false)
check(mtn("co","--branch=testbranch","testbr"),0, false, false)

writefile("testbr/A", "new A\n")
tsha1 = sha1("testbr/A")
check(indir("testbr", mtn("ci", "-m", "a changed")), 0, false, false)

writefile("testbr/B", "new B\n")
tsha2 = sha1("testbr/B")
check(indir("testbr", mtn("ci", "-m", "b changed")), 0, false, false)

check(mtn_cvs("--branch=testbranch","push"),0, true, true)

check(indir("cvs-test", cvs("up")), 0, false, false)
check(sha1("cvs-test/A") == tsha1)
check(sha1("cvs-test/B") == tsha2)
