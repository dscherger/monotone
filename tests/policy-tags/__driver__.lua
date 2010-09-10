mtn_setup()

check(mtn("create_project", "test_project"), 0, false, false)

addfile("foo", "bar")

check(mtn("create_branch", "test_project.testbranch"), 0, false, false)

commit("test_project.testbranch", "foobar")

revid = base_revision()

check(mtn("tag", revid, "test_project.mytag"), 0, false, false)

check(mtn("ls", "tags"), 0, true)
check(qgrep("test_project.mytag", "stdout"))
check(qgrep(revid:sub(0,10) .. ".*test_project.testbranch", "stdout"))


check(mtn("db", "execute", "update db_vars set name = cast('testproj' as blob) " ..
	  "where name = cast('test_project' as blob)"), 0, false, false)

check(mtn("ls", "tags"), 0, true)
check(qgrep("testproj.mytag", "stdout"))
check(qgrep(revid:sub(0,10) .. ".*testproj.testbranch", "stdout"))