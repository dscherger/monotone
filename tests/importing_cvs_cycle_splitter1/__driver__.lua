
mtn_setup()

-- Test splitting a normal cycle:
--
--    A -> B -> C 
--    ^         |
--    |         |
--    +---------+
--
-- As blob A has the largest gap, this cycle should be split into:
--
--   A(1) -> B -> C -> A(2)

-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

-- import into monotone
check(mtn("--branch=testbranch", "cvs_import", "cvs-repository/test"), 0, false, false)

-- check for correct ordering of the commits
check(mtn("checkout", "--branch=testbranch", "mtcodir"), 0, false, false)
check(indir("mtcodir", mtn("log", "--no-graph", "--no-files")), 0, true, false)
check(grep("blob", "stdout"), 0, true, false)
check(samelines("stdout", {"blob A", "blob C", "blob B", "blob A"}))

