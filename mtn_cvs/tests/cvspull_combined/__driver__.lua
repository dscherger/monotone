
include("/common/cvs.lua")
mtn_setup()
cvs_setup()

mkdir("cvstemp")
writefile("cvstemp/A", "initial import\n")
check(indir("cvstemp", cvs("import", "-m", "import", "test", "vtag", "rtag")), 0, false, false)
remove("cvstemp")

check(cvs("co", "test")), 0, false, false)
writefile("test/B", "file added\n")
check(indir("test", cvs("add", "B")), 0, false, false)
os.execute("sleep 1")
check(indir("test", cvs("ci", "-m", "B added")), 0, false, false)

check(indir("test", cvs("delete", "-f", "A")), 0, false, false)
os.execute("sleep 1")
check(indir("test", cvs("ci", "-m", "A removed")), 0, false, false)

writefile("test/B", "something different\n")
os.execute("sleep 1")
check(indir("test", cvs("ci", "-m", "B changed")), 0, false, false)

mkdir("test/dir")
check(indir("test", cvs("add", "dir")), 0, false, false)
writefile("test/dir/D", "some subdir\n")
check(indir("test", cvs("add", "dir/D")), 0, false, false)
os.execute("sleep 1")
check(indir("test", cvs("ci", "-m", "dir/D added")), 0, false, false)

writefile("test/A", "revived\n")
check(indir("test", cvs("add", "A")), 0, false, false)
os.execute("sleep 1")
check(indir("test", cvs("ci", "-m", "A readded")), 0, false, false)

check(mtn_cvs("--branch=testbranch", "cvs_pull", "cvsroot?", "test"), 0, false, false)

-- check presence of files

check(mtn("automate", "get_file", sha1sum(...)), 0, false, false)
--AT_CHECK(MTN automate get_file $TSHA1, [], [ignore])
--AT_CHECK(MTN automate get_file $TSHA2, [], [ignore])
--AT_CHECK(MTN automate get_file $TSHA3, [], [ignore])
--AT_CHECK(MTN automate get_file $TSHA4, [], [ignore])

--# also check that history is okay -- has a unique head, and it's the
--# right one.

--AT_CHECK(MTN checkout --branch=testbranch mtcodir, [], [ignore], [ignore])
--AT_CHECK(cmp d_readd mtcodir/A)
--AT_CHECK(cmp d_change mtcodir/B)
--AT_CHECK(cmp d_subdir mtcodir/dir/D)
