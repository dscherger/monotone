
mtn_setup()

-- Test splitting a normal cycle:
--
--    A -> B -> C 
--    ^         |
--    |         |
--    +---------+
--
-- This time, as opposed to the cycle_splitter1 test, the last commit
-- on file bar, i.e. blob A, has manually been 'adjusted' to carry the
-- very same timestamp (and commit id) as the commit of file foo in
-- blob A. Thus blob A looks very much like all other blobs: all events
-- carry the very same timestamp. Thus it's not possible to split by
-- the largest gap.
--
-- To solve this, the importer would have to adjust the timestamps of
-- the single files, before attempting to split cycles.


-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

-- import into monotone
check(mtn("--branch=testbranch", "cvs_import", "cvs-repository/test"), 0, false, false)

-- check for correct ordering of the commits
check(mtn("checkout", "--branch=testbranch", "mtcodir"), 0, false, false)
check(indir("mtcodir", mtn("log", "--no-graph", "--no-files")), 0, true, false)
check(grep("blob", "stdout"), 0, true, false)
check(samelines("stdout", {"blob A", "blob C", "blob B", "blob A"}))

