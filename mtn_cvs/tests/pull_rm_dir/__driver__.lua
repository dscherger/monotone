
include("/common/cvs.lua")
mtn_setup()
cvs_setup()

mkdir("cvstemp")
mkdir("cvstemp/dir")
writefile("cvstemp/A", "initial A\n")
writefile("cvstemp/dir/B", "initial B\n")
check(indir("cvstemp", cvs("import", "-m", "import", "test", "vtag", "rtag")), 0, false, false)
remove("cvstemp")

check(mtn_cvs("--branch=testbranch", "pull", cvsroot, "test"), 0, false, false)

check(cvs("co", "test"), 0, false, false)
check(indir("test/dir", cvs("delete", "-f", "B")), 0, false, false)
check(indir("test", cvs("ci", "-m", "dir/B removed")), 0, false, false)

check(mtn("co", "--branch=testbranch", "mtcodir"), 0, false, false)

check(exists("mtcodir/dir"))

check(mtn_cvs("--branch=testbranch", "pull", cvsroot, "test"), 0, false, false)

check(indir("mtcodir", mtn("up")), 0, false, false)

intermediate_exist = (not exists("mtcodir/dir"))

writefile("test/dir/B", "new B\n")
tsha1 = sha1("test/dir/B")
check(indir("test/dir", cvs("add", "B")), 0, false, false)
check(indir("test", cvs("ci", "-m", "dir/B re-added")), 0, false, false)

check(mtn_cvs("--branch=testbranch", "pull", cvsroot, "test"), 0, false, false)

check(indir("mtcodir", mtn("up")), 0, false, false)

check(exists("mtcodir/dir/B"))
check(sha1("mtcodir/dir/B") == tsha1)

xfail(intermediate_exist) -- need to fix line 27!  (Maybe introduce an explicit --prune (empty dirs) option?)

