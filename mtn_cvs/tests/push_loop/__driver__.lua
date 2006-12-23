-- CVS push with fork and merge

include("/common/cvs.lua")
mtn_setup()
cvs_setup()

-- create a test project

mkdir("cvstemp")
writefile("cvstemp/A","A\n")
writefile("cvstemp/B","B\n")
check(indir("cvstemp", cvs("import","-m","initial import", "test", "vendor_tag", "initial_import")), 0, false, false)

-- change the repository in various ways
check(mtn_cvs("--branch=testbranch","pull",cvsroot,"test"),0, false,false)
check(mtn("co","--branch=testbranch","test"),0, false, false)
check(mtn("co","--branch=testbranch","test2"),0, false, false)

check(indir("test", mtn("drop", "A")), 0, false, false)
check(indir("test", mtn("ci", "-m", "a dropped")), 0, false, false)

check(mtn("automate", "heads"), 0, true, false)
canonicalize("stdout")
left = readfile("stdout")
left = string.sub(left,0,40)

writefile("test2/C", "C\n")
check(indir("test2", mtn("add", "C")), 0, false, false)
check(indir("test2", mtn("ci", "-m", "c added")), 0, false, false)

check(mtn("merge"), 0, false, false)
check(mtn_cvs("--branch=testbranch","push"),0, false,false)
-- no change yet
check(exists(cvsroot .. "/test/A,v"))
check(not exists(cvsroot .. "/test/C,v"))

check(mtn_cvs("--branch=testbranch","-r",left,"push"),0, false,false)
check(exists(cvsroot .. "/test/Attic/A,v"))
check(exists(cvsroot .. "/test/C,v"))
