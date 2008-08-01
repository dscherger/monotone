
mtn_setup()

check(get("cvs-repository"))

-- A carefully handcrafted CVS repository, exercising the cycle splitter: We
-- have 2x three files and some pretty nasty cyclic dependencies:
--
--    file1:  A -> B -> C
--    file2:       B -> C -> A
--    file3:            C -> A -> B
--    file1b: A -> B -> C
--    file2b:      B -> C -> A
--    file3b:           C -> A -> B
--    file1c: A
--    file2c:      B
--    file3c:           C
--
-- The timestamps are for the different commit events in the files do not
-- match, so that the cycle splitter cannot split by timestamp. As opposed
-- to the cycle_splitter5 test, the timestamps of the *c,v files are set
-- in between the type 2 and type 3 events, so that the cycle splitter
-- cannot split this cycle because it refuses to split blobs with type 1
-- events it doesn't know where to put.

-- import into monotone and check presence of files
xfail(mtn("--branch=test", "cvs_import", "--debug", "cvs-repository/test"), 0, false, false)

