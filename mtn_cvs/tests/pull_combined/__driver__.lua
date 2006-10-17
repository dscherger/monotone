
include("/common/cvs.lua")
mtn_setup()
cvs_setup()

mkdir("cvstemp")
writefile("cvstemp/A", "initial import\n")
tsha0 = sha1("cvstemp/A")
check(indir("cvstemp", cvs("import", "-m", "import", "test", "vtag", "rtag")), 0, false, false)
remove("cvstemp")

check(cvs("co", "test"), 0, false, false)
writefile("test/B", "file added\n")
tsha1 = sha1("test/B")
check(indir("test", cvs("add", "B")), 0, false, false)
os.execute("sleep 1")
check(indir("test", cvs("ci", "-m", "B added")), 0, false, false)

check(indir("test", cvs("delete", "-f", "A")), 0, false, false)
os.execute("sleep 1")
check(indir("test", cvs("ci", "-m", "A removed")), 0, false, false)

writefile("test/B", "something different\n")
tsha2 = sha1("test/B")
os.execute("sleep 1")
check(indir("test", cvs("ci", "-m", "B changed")), 0, false, false)

mkdir("test/dir")
check(indir("test", cvs("add", "dir")), 0, false, false)
writefile("test/dir/D", "some subdir\n")
tsha3 = sha1("test/dir/D")
check(indir("test", cvs("add", "dir/D")), 0, false, false)
os.execute("sleep 1")
check(indir("test", cvs("ci", "-m", "dir/D added")), 0, false, false)

writefile("test/A", "revived\n")
tsha4 = sha1("test/A")
check(indir("test", cvs("add", "A")), 0, false, false)
os.execute("sleep 1")
check(indir("test", cvs("ci", "-m", "A readded")), 0, false, false)

check(mtn_cvs("--branch=testbranch", "pull", cvsroot, "test"), 0, false, false)

-- check presence of files

check(mtn("automate", "get_file", tsha0), 0, false)
check(mtn("automate", "get_file", tsha1), 0, false)
check(mtn("automate", "get_file", tsha2), 0, false)
check(mtn("automate", "get_file", tsha3), 0, false)
check(mtn("automate", "get_file", tsha4), 0, false)

-- also check that history is okay -- has a unique head, and it's the
-- right one.

check(mtn("checkout", "--branch=testbranch", "mtcodir"), 0, false, false)
check(sha1("mtcodir/A") == tsha4)
check(sha1("mtcodir/B") == tsha2)
check(sha1("mtcodir/dir/D") == tsha3)
