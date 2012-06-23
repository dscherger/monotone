
mtn_setup()

-- This test is a bug report, now fixed. The problem was:
-- 
-- inodeprints tries to update its cache for all files in the complete
-- manifest, but a restricted commit can succeed with missing files if
-- they are excluded. subsequently the inodeprint update fails because
-- it can't build a complete manifest due to the missing files.

addfile("file1", "file1")

commit()

-- enable inodeprints mode
check(mtn("refresh_inodeprints"), 0, false, false)

addfile("file2", "file2")

-- create a missing file

remove("file1")

-- restricted commit of file2 succeeds with file1 missing

check(mtn("commit", "--message=file2", "file2"),  0, false, false)
