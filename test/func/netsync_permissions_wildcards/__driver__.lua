
include("/common/netsync.lua")
mtn_setup()

netsync.setup()

check(get("read-permissions"))
check(get("write-permissions"))

srv = netsync.start({"--confdir=."}, 2, false)

-- Try pushing just one exact branch
addfile("testfile1", "test file 1")
commit("testbranch", "testfile")
srv:push("testbranch", 1)

-- will fail if the rev wasn't synced
srv:stop()
check(mtn2("update"), 0, false, false)

-- Try pushing just branches matching a wild card
addfile("testfile2", "test file 2")
commit("testbranch", "testfile")

-- if we restart the server before doing the add, the add
-- will fail when it tried to unregister the workspace with
-- the old test2.db (set as the workspace db during the update)
srv = netsync.start({"--confdir=."}, 2, false)

srv:push("testbra*", 1)

srv:stop()
check(mtn2("update"), 0, false, false)

