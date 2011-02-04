
check(raw_mtn("ls", "workspaces"), 1, false, true)
check(qgrep("no database specified", "stderr"))

check(raw_mtn("db", "init", "-d", "test.mtn"), 0, false, false)

check(raw_mtn("ls", "workspaces", "-d", "test.mtn"), 0, true, false)
check(samelines("stdout", {"no known valid workspaces"}))

check(raw_mtn("setup", "-d", "test.mtn", "-b", "test.branch1", "work1"), 0, false, false)

check(raw_mtn("ls", "workspaces", "-d", "test.mtn"), 0, true, false)
check(qgrep("test.branch1.+in.+list_workspaces\/work1", "stdout"))

check(raw_mtn("setup", "-d", "test.mtn", "-b", "test.branch2", "work2"), 0, false, false)
check(rename("work1", "work3"))

check(raw_mtn("ls", "workspaces", "-d", "test.mtn"), 0, true, false)
check(qgrep("test.branch2.+in.+list_workspaces\/work2", "stdout"))
check(not qgrep("test.branch1.+in.+list_workspaces\/work1", "stdout"))

check(indir("work3", raw_mtn("register_workspace", "-d", "../test.mtn")), 0, false, false)

check(raw_mtn("ls", "workspaces", "-d", "test.mtn"), 0, true, false)
check(qgrep("test.branch1.+in.+list_workspaces\/work3", "stdout"))

