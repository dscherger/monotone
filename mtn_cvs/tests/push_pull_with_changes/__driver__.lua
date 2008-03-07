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
check(mtn("co","--branch=testbranch","test-mtn-wc"),0, false, false)

writefile("test-mtn-wc/A","A\n modified in mtn\n")

check(indir("test-mtn-wc", mtn("commit", "-m", "A modified")), 0, false, false)

check(cvs("co", "-d", "test-cvs-wc", "test"), 0, false, false)

writefile("test-cvs-wc/A","modified in cvs\nA")
writefile("test-cvs-wc/B","modified in cvs\nB")

check(indir("test-cvs-wc", cvs("commit", "-m", "A and B modified")), 0, false, false)

check(mtn_cvs("--branch=testbranch","pull",cvsroot,"test"),0, false,false)

check(mtn("merge"), 0, false, false)

check(mtn_cvs("--branch=testbranch","push",cvsroot,"test"),0, false,false)

check(indir("test-cvs-wc", cvs("up")), 0, false, false)

Afile = readfile("test-cvs-wc/A")

check(Afile == "modified in cvs\nA\n modified in mtn\n", 0, false, false)

writefile("test-cvs-wc/A","modified again\n")
writefile("test-cvs-wc/B","modified again\n")
check(indir("test-cvs-wc", cvs("commit", "-m", "A modified")), 0, false, false)

writefile("test-cvs-wc/A","and again\n")
writefile("test-cvs-wc/B","and again\n")
check(indir("test-cvs-wc", cvs("commit", "-m", "A again")), 0, false, false)

skip_if(true)   -- the following line causes mtn_cvs to freeze

check(mtn_cvs("--branch=testbranch","pull",cvsroot,"test"),0, false,false)

check(indir("test-mtn-wc", mtn("up")), 0, false, false)

Afile = readfile("test-mtn-wc/A")
check(Afile == "and again\n", 0, false, false)
Bfile = readfile("test-mtn-wc/B")
check(Bfile == "and again\n", 0, false, false)
