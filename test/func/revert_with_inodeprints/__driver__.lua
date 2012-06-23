-- Test for issue 207; partial revert succeeds without inodeprints,
-- fails with. Now fixed.

mtn_setup()

addfile("file1", "file1")
addfile("file2", "file2")

commit()

-- enable inodeprints mode
check(mtn("refresh_inodeprints"), 0, false, false)

-- create two missing files

remove("file1")
remove("file2")

-- revert only one file

check(mtn("revert", "file2"),  0, false, false)
