-- Demonstrate --move-conflicting-paths
--
-- There are two 'update' use cases:
--
-- 1) user has unknown files and/or directories with the same names as
--    added files or directories in a new revision.
--
-- 2) user has unknown files in a directory that is dropped in a new
--    revision
--
-- 3) user has known files with changes that are dropped in a new
--    revision
--
--  --move-conflicting-paths handles cases 1, 2; but not 3.
--
-- There are also 'checkout', 'pluck', and 'pivot_root' use cases. The
-- core machinery is the same as for the above, so we just do one test
-- for each of these.

mtn_setup()

--  Use case 1.
addfile("somefile", "somefile content")
mkdir("somedir")
addfile("somedir/anotherfile", "anotherfile content")
commit("testbranch", "base")
base = base_revision()

addfile("thirdfile", "thirdfile content")
addfile("somedir/fourthfile", "fourthfile content")
commit("testbranch", "one")
rev_one = base_revision()

revert_to(base)
writefile("thirdfile", "thirdfile content 2")
writefile("somedir/fourthfile", "fourthfile content 2")

-- reports conflicts with unversioned files
check(mtn("update"), 1, nil, true)
check(qgrep("unversioned", "stderr"))

-- moves them out of the way
check(mtn("update", "--move-conflicting-paths"), 0, nil, true)
check(qgrep("moved conflicting", "stderr"))
check(readfile("_MTN/resolutions/somedir/fourthfile")=="fourthfile content 2")

remove("_MTN/resolutions")

--  Use case 2.
revert_to(rev_one)
check(mtn("drop", "somedir/fourthfile"), 0, nil, true)
check(mtn("drop", "somedir/anotherfile"), 0, nil, true)
check(mtn("drop", "somedir"), 0, nil, true)
commit("testbranch", "two")

revert_to(rev_one)

writefile("somedir/fifthfile", "fifthfile content 1")

-- reports conflicts with unversioned files
check(mtn("update"), 1, nil, true)
check(qgrep("cannot drop non-empty directory", "stderr"))

-- moves them out of the way
check(mtn("update", "--move-conflicting-paths"), 0, nil, true)
check(qgrep("moved conflicting", "stderr"))
check(readfile("_MTN/resolutions/somedir/fifthfile")=="fifthfile content 1")

-- FIXME: do checkout, pluck, pivot_root

-- end of file
