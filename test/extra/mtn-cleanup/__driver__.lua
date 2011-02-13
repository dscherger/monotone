mtn_setup()

-- Get helper scripts
check(get("run-mtn-cleanup"))

-- We do everything inside an inner workspace.  mtn-cleanup IS a dangerous
-- command, and will happily wipe away any database that's in the workspace
check(mtn("setup", "--branch=testbranch", "workspace"), 0, false, false)
writefile("workspace/test1", "foo")
check(indir("workspace", mtn("add", "test1")), 0, false, false)
check(indir("workspace", mtn("commit",
			     "--message", "blah-blah",
			     "--branch", "test1")),
      0, false, false)
writefile("workspace/test1", "foobar")
writefile("workspace/test2", "bar")
check(indir("workspace", mtn("add", "test2")), 0, false, false)
writefile("workspace/test3", "baz")

check(indir("workspace", {"./run-mtn-cleanup",srcdir,test.root}),
      0, true, false)
check(exists("workspace/test1"))
xfail(exists("workspace/test2"))
xfail(exists("workspace/test3"))
