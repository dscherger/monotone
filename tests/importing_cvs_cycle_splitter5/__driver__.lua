
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
--
-- The timestamps are for the different commit events in the files do not
-- quite match, so that the cycle splitter cannot split by timestamp.
-- However, in this case, there are only type 1 events we can clearly
-- put to one or the other side (by its timestamps), so the cycle is
-- splittable.
--
-- Check the cycle_splitter6 test, for additional cruelty.

-- import into monotone and check presence of files
check(mtn("--branch=test", "cvs_import", "--debug", "cvs-repository/test"), 0, false, false)

