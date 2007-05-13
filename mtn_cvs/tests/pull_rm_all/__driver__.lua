
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
writefile("cvstemp/dir/B", "initial B\n")
check(indir("cvstemp", cvs("import", "-m", "import", "test", "vtag", "rtag")), 0, false, false)
remove("cvstemp")

check(mtn_cvs("--branch=testbranch", "pull", cvsroot, "test"), 0, false, false)

check(cvs("co", "test"), 0, false, false)
check(indir("test/dir", cvs("delete", "-f", "B")), 0, false, false)
check(indir("test", cvs("ci", "-m", "dir/B removed")), 0, false, false)

xfail(mtn_cvs("--branch=testbranch", "pull", cvsroot, "test"))
check(mtn("co", "--branch=testbranch", "mtcodir"), 0, false, false)

check(not exists("mtcodir/dir/B"))
