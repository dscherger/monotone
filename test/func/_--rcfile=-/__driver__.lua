
mtn_setup()

check(get("foo.rc"))

addfile("testfile", "blah blah")
check(mtn("--rcfile=-", "commit", "--message=foo", "--branch=testbranch"), 0, false, true, {"foo.rc"})

check(qgrep("BOOGA", "stderr"))
