-- take over a modified CVS checkout and push changes into CVS

include("/common/cvs.lua")
mtn_setup()
cvs_setup()

mkdir("cvstemp")
writefile("cvstemp/A", "initial import\n")
tsha0 = sha1("cvstemp/A")
check(indir("cvstemp", cvs("import", "-m", "import", "test", "vtag", "rtag")), 0, false, false)
remove("cvstemp")

-- change the repository in various ways

check(cvs("co", "test"), 0, false, false)
writefile("test/B", "file added\n")
tsha1 = sha1("test/B")
check(indir("test", cvs("add", "B")), 0, false, false)
os.execute("sleep 1")
check(indir("test", cvs("ci", "-m", "B added")), 0, false, false)

check(indir("test", cvs("delete","-f","A")), 0,false,false)
os.execute("sleep 1")
check(indir("test", cvs("ci", "-m", "A removed")), 0, false, false)

writefile("test/B", "something different\n")
tsha2 = sha1("test/B")

-- we need the root option to not look for a _MTN directory above the test
check(indir("test",mtn_cvs("--root=.", "--branch=testbranch", "takeover", "test")), 0, false, false)

check(mtn_cvs("--branch=testbranch","push"), 0, false, false)

---------------
-- check presence of files

check(mtn("automate", "get_file", tsha2), 0, false)

-- also check that CVS sees this newly changed file

-- FIXME: win32
os.execute("mv test test.old")

check(cvs("co", "test"), 0, false, false)
check(sha1("test/B") == tsha2)
