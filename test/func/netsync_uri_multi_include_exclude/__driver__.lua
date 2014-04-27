-- Show that multiple include and exclude patterns are supported

includecommon("netsync.lua")
mtn_setup()
netsync.setup()

addfile("file1", "foo")
commit("branch1")

addfile("file2", "bar")
commit("branch2")

addfile("file3", "baz")
commit("branch3")

addfile("file4", "4")
commit("branch4")

srv = netsync.start()

check(mtn2("pull", srv.url .. "?-branch1;branch2;-branch3;branch4"), 0, nil, true)
check(qgrep("include pattern  '{branch2,branch4}'", "stderr"))
check(qgrep("exclude pattern  '{branch1,branch3}'", "stderr"))

check(mtn2("ls", "branches"), 0, true)

check(not (qgrep("^branch1$", "stdout")))
check(qgrep("^branch2$", "stdout"))
check(not (qgrep("^branch3", "stdout")))
check(qgrep("^branch4", "stdout"))

srv:stop()
