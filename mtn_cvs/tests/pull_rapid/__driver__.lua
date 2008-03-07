
include("/common/cvs.lua")
mtn_setup()
cvs_setup()

-- fake a test project

mkdir(cvsroot .. "/test")

-- change the repository in various ways

check(cvs("co", "test"),0,false,false)
writefile("test/A","fast A\n")
writefile("test/B","fast B\n")
writefile("test/C","fast C\n")
writefile("test/D","fast D\n")
check(indir("test", cvs("add","A")),0,false,false)
check(indir("test", cvs("ci","-m","fast A add")),0,false,false)
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

check(mtn_cvs("--branch=testbranch","pull",cvsroot,"test"),0, false,false)

-- also check that history is okay -- has a unique head

check(mtn("checkout","--branch=testbranch","mtcodir"),0,false)
