includecommon("selectors.lua")
mtn_setup()

addfile("testfile", "blah blah")
commit("testbranch", "this is revision one")
REV1=base_revision()

writefile("testfile", "stuff stuff")
commit("testbranch", "this is revision number two")
REV2=base_revision()

writefile("testfile", "foobar")
commit("testbranch", "this is revision number three")
REV3=base_revision()

-- no updates yet

check(mtn("automate", "select", "u:"), 1, true, false)

-- update to rev1 from rev3

check(mtn("update", "-r", REV1), 0, true, false)
selmap("u:", {REV3})

-- update to rev2 from rev1

check(mtn("update", "-r", REV2), 0, true, false)
selmap("u:", {REV1})

-- update back to rev1 from rev2

check(mtn("update", "-r", "u:"), 0, true, false)
selmap("u:", {REV2})

-- finally update back to rev3

check(mtn("update"), 0, true, false)
selmap("u:", {REV1})

-- log the update rev

check(mtn("log", "-r", "u:", "--no-graph", "--brief"), 0, true, false)
check(qgrep(REV1, "stdout"))
check(not qgrep(REV2, "stdout"))
check(not qgrep(REV3, "stdout"))

-- log back to the update rev

check(mtn("log", "--to", "u:", "--no-graph", "--brief"), 0, true, false)
check(not qgrep(REV1, "stdout"))
check(qgrep(REV2, "stdout"))
check(qgrep(REV3, "stdout"))
