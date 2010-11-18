
mtn_setup()

-- need a valid revision id
check(mtn("au", "get_extended_manifest_of", string.rep("0123", 10)), 1, false, true)
check(qgrep("no revision " .. string.rep("0123", 10) .. " found in database", "stderr"))

addfile("foofile", "blah")
adddir("bardir")
check(mtn("attr", "set", "bardir", "barprop", "barpropval"), 0, false, false)
commit()
R1=base_revision()

writefile("foofile", "blahblah")
check(mtn("attr", "drop", "bardir", "barprop"), 0, false, false)
commit()
R2=base_revision()

check(mtn("mv", "foofile", "foo-file"), 0, false, false)
check(mtn("drop", "bardir"), 0, false, false)
commit()
R3=base_revision()

get("expected1")
check(mtn("au", "get_extended_manifest_of", R1), 0, true, false)
check(samefile("stdout", "expected1"))

get("expected2")
check(mtn("au", "get_extended_manifest_of", R2), 0, true, false)
check(samefile("stdout", "expected2"))

get("expected3")
check(mtn("au", "get_extended_manifest_of", R3), 0, true, false)
check(samefile("stdout", "expected3"))
