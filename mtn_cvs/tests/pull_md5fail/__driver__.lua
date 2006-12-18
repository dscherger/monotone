-- takeover modified, cvs commit, pull (causing MD5 failure)
include("/common/cvs.lua")
mtn_setup()

cvs_setup()

check(get("test", cvsroot.."/test"))

check(cvs("co", "test"), 0, false, false)
writefile("test/A", "new contents\nrevived\n")
tsha1 = sha1("test/A")
check(indir("test", mtn_cvs("--root=.", "--branch=testbranch", "takeover", "test")), 0, false, false)

writefile("test/A", "new contents version 2\nrevived\n")
tsha2 = sha1("test/A")
check(indir("test", cvs("ci", "-m", "put different data into CVS")),0, false, false)
check(mtn_cvs("--branch=testbranch", "pull"), 0, false, false)

-- check for presence of file
check(mtn("automate", "get_file", tsha1), 0, false)
check(mtn("automate", "get_file", tsha2), 0, false)

-- should do nothing
check(mtn_cvs("--branch=testbranch", "pull"), 0, false, false)
