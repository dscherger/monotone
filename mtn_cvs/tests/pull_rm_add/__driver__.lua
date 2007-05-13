
include("/common/cvs.lua")
mtn_setup()
cvs_setup()

function countMatch(data, match)
    count = 0
    for word in string.gmatch(data, match) do
        count = count + 1
    end
    return count
end

mkdir("cvstemp")
mkdir("cvstemp/dir")
writefile("cvstemp/A", "initial A\n")
writefile("cvstemp/dir/B", "initial B\n")
check(indir("cvstemp", cvs("import", "-m", "import", "test", "vtag", "rtag")), 0, false, false)
remove("cvstemp")

check(mtn_cvs("--branch=testbranch", "pull", cvsroot, "test"), 0, false, false)

check(cvs("co", "test"), 0, false, false)
check(indir("test/dir", cvs("delete", "-f", "B")), 0, false, false)
check(indir("test", cvs("ci", "-m", "file B removed")), 0, false, false)

check(mtn_cvs("--branch=testbranch", "pull", cvsroot, "test"), 0, false, false)
check(mtn("co", "--branch=testbranch", "mtcodir"), 0, false, false)

check(not exists("mtcodir/dir/B"))

writefile("test/dir/B", "new B\n")
tsha1 = sha1("test/dir/B")
check(indir("test/dir", cvs("add", "B")), 0, false, false)
check(indir("test", cvs("ci", "-m", "file B readded")), 0, false, false)

check(mtn_cvs("--branch=testbranch", "pull", cvsroot, "test"), 0, false, false)

check(indir("mtcodir", mtn("up")), 0, false, false)

check(exists("mtcodir/dir/B"))
check(sha1("mtcodir/dir/B") == tsha1)

check(indir("mtcodir", mtn("log")), 0, true, false)
canonicalize("stdout")
log = readfile("stdout")

check(countMatch(log, "file B removed") == 1, true, false, false)
check(countMatch(log, "file B readded") == 1, true, false, false)
check(countMatch(log, "import") == 1, true, false, false)
