
mtn_setup()

testname="add,remove,cleanup_registered_workspaces"

check(mtn("ls", "vars"), 0, true, false)
check(qgrep("database: known-workspaces .+/"..testname, "stdout"))

check(mtn("register_workspace", "foo"), 0, false, false)
check(mtn("ls", "vars"), 0, true, false)
check(qgrep("database: known-workspaces .+/"..testname, "stdout"))
check(qgrep(testname.."/foo", "stdout"))

check(nodb_mtn("db", "init", "-d", "other.mtn"), 0, false, false)

check(nodb_mtn("ls", "vars", "-d", "other.mtn"), 0, true, false)
check(not qgrep("database: known-workspaces", "stdout"))

check(nodb_mtn("setup", "-b", "bar", "-d", "other.mtn", "bar"), 0, false, false)
check(mtn("register_workspace", "bar"), 0, false, false)
check(mtn("ls", "vars"), 0, true, false)
check(qgrep(testname .. "/bar", "stdout"))

check(mtn("register_workspace", "baz"), 0, false, false)

check(mtn("unregister_workspace", "foo"), 0, false, false)
check(mtn("ls", "vars"), 0, true, false)
check(not qgrep(testname .. "/foo", "stdout"))

check(mtn("cleanup_workspace_list"), 0, false, false)
check(mtn("ls", "vars"), 0, true, false)

check(not qgrep(testname .. "/bar", "stdout"))
check(not qgrep(testname .. "/baz", "stdout"))

check(qgrep("database: known-workspaces .+/"..testname, "stdout"))

