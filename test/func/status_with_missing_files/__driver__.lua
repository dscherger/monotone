
mtn_setup()

addfile("testfile1", "foo")
addfile("testfile2", "bar")
commit()

remove("testfile1")
remove("testfile2")

-- status should successfully report on the status of things regardless
-- of the status of those things -- i.e. it should report missing files
-- as such rather than failing on them.

-- status should list all missing files before failing 
-- if/when there are missing files

check(mtn("status"), 0, true, true)
check(qgrep("testfile1", "stdout"))
check(qgrep("testfile2", "stdout"))

-- Also ensure these missing files aren't listed again as patched (as
-- they erroneously used to be at some point during development).
check(not qgrep("patched  testfile1", "stdout"))
check(not qgrep("patched  testfile2", "stdout"))
