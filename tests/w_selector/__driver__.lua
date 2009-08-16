
include("common/selectors.lua")

mtn_setup()

-- empty revision
selmap("w:", {""})

-- clean ws
addfile("testfile", "blah blah")
commit()
r=base_revision()
selmap("w:", {r})

-- ws with changes
writefile("testfile", "stuff stuff")
selmap("w:", {r})
commit()

-- back to parent
check(mtn("update", "-rp:"), 0, false, false)
selmap("w:", {r})
