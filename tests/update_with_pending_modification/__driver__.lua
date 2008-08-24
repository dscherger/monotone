
mtn_setup()

-- add a first file
addfile("file1", "contents of file1")
commit()

-- store the newly created revision id
REV1=base_revision()

-- check(mtn("--branch", "testbranch", "co", "codir"), 0, false, false)
-- writefile("codir/file2", "contents of file2")

-- add another file and commit
addfile("file2", "contents of file2")
commit()

-- change that new file
writefile("file2", "new contents of file2")

-- .. and attempt to update to the previous revision, which didn't have file2.
-- In mtn 0.40 and earlier, this simply drops file2 and all changes to it.
-- Now it reports a conflict
-- See bug #15058

check(mtn("update", "-r", REV1), 1, nil, true)
check(qgrep("conflict: file 'file2' dropped on the right, changed on the left", "stderr"))

