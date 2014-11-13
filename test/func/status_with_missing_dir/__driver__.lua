
mtn_setup()

mkdir("dir")
addfile("dir/testfile1", "foo")
addfile("dir/testfile2", "bar")
commit()

remove("dir/testfile1")
remove("dir/testfile2")
remove("dir")

-- status should successfully report on the status of things regardless
-- of the status of those things -- i.e. it should report missing files
-- as such rather than failing on them.

-- status should list all missing files before failing 
-- if/when there are missing files

check(mtn("status"), 0, true, true)
check(qgrep("dir/testfile1", "stdout"))
check(qgrep("dir/testfile2", "stdout"))

-- Also ensure these missing files aren't listed again as patched (as
-- they erroneously used to be at some point during development).
check(not qgrep("patched  dir/testfile1", "stdout"))
check(not qgrep("patched  dir/testfile2", "stdout"))
