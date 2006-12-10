
include("/common/cvs.lua")
mtn_setup()
cvs_setup()

-- fake a test project

mkdir(cvsroot .. "/test")

-- change the repository in various ways

mkdir(cvsroot .. "/test2")

mkdir("test.cvstemp")
check(indir("test.cvstemp", cvs("co","CVSROOT")),0,false,false)
append("test.cvstemp/CVSROOT/modules", "test1 test\n")
append("test.cvstemp/CVSROOT/modules", "test3 test2 &test1\n")
check(indir("test.cvstemp/CVSROOT", cvs("ci","-m","module setup")),0,false,false)

check(cvs("co", "test3"),0,false,false)
writefile("test3/AA","initial AA\n")
check(indir("test3", cvs("add","AA")),0,false,false)
check(indir("test3", cvs("ci","-m","AA added")),0,false,false)

mkdir("test3/sub2")
check(indir("test3", cvs("add","sub2")),0,false,false)
writefile("test3/sub2/BB","initial BB\n")
check(indir("test3", cvs("add","sub2/BB")),0,false,false)
check(indir("test3", cvs("ci","-m","sub2/BB added")),0,false,false)

writefile("test3/test1/A","cross module change\n")
check(indir("test3/test1", cvs("add","A")),0,false,false)
check(indir("test3/test1", cvs("ci","-m","cross module change of A")),0,false,false)

check(cvs("co", "test"),0,false,false)
writefile("test/A","fast A\n")
writefile("test/B","fast B\n")
writefile("test/C","fast C\n")
writefile("test/D","fast D\n")
check(indir("test", cvs("ci","-m","fast A change")),0,false,false)
check(indir("test", cvs("add","C")),0,false,false)
check(indir("test", cvs("ci","-m","fast C add")),0,false,false)
check(indir("test", cvs("add","B")),0,false,false)
check(indir("test", cvs("ci","-m","fast B add")),0,false,false)
check(indir("test", cvs("add","D")),0,false,false)
check(indir("test", cvs("ci","-m","fast D add")),0,false,false)
writefile("test/A","fast A change\n")
writefile("test/B","fast B change\n")
writefile("test/C","fast C change\n")
writefile("test/D","fast D change\n")
check(indir("test", cvs("ci","-m","fast D change","D")),0,false,false)
check(indir("test", cvs("ci","-m","fast C change","C")),0,false,false)
check(indir("test", cvs("ci","-m","fast B change","B")),0,false,false)
check(indir("test", cvs("ci","-m","fast A change","A")),0,false,false)

-- pull into monotone

check(mtn_cvs("--branch=testbranch","pull",cvsroot,"test3"),0, false,false)

-- also check that history is okay -- has a unique head

check(mtn("checkout","--branch=testbranch","mtcodir"),0,false)
