mtn_setup()

addfile("file1", "original file1")
commit()

-- setup 
check(mtn("rename", "--bookkeep-only", "file1", "file2"), 0, true, true)
addfile("file1", "new file1")

-- first check; revert the new file which will end up reverting both changes
-- XXX is this really desired behaviour?

-- currently each name is matched against both workspace rosters. here "file1"
-- from the old roster has been renamed to "file2" in the new roster and "file1"
-- has also been added to the new roster. given these two rosters the name "file1"
-- identifies two different files.

check(mtn("revert", "file1"), 0, true, true)
-- check results

-- setup 
check(mtn("rename", "--bookkeep-only", "file1", "file3"), 0, true, true)
addfile("file1", "new file1")

-- second check; revert the renamed file which will.. uh.. trip an I()
-- this should fail because we would otherwise have two file1's
-- but it should fail more gracefully than with an I()
xfail(check(mtn("revert", "file3"), 3, true, true))
