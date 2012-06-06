-- Test the case of update where there is a dropped/modified conflict

mtn_setup()

-- add a first file
addfile("file1", "contents of file1")
commit()

REV1=base_revision()

-- add another file, then change it
addfile("file2", "contents of file2")
commit()

writefile("file2", "new contents of file2")

-- update to the previous revision, which didn't have file2. This
-- looks like file2 is dropped on one side (in rev 1), and modified on
-- the other (the workspace), so we get a dropped/modified conflict.

check(mtn("update", "-r", REV1), 1, true, true)
check(qgrep("mtn: conflict: file 'file2'"), "stderr")
check(qgrep("mtn: modified on the left, named file2"), "stderr")
check(qgrep("mtn: dropped on the right"), "stderr")
check(qgrep("mtn: misuse: merge failed due to unresolved conflicts"), "stderr")

-- Since this is a workspace merge, we can't resolve the conflict; the
-- modified file must be committed first.

-- end of file
