
mtn_setup()

check(get("cvs-repository"))

-- A carefully handcrafted CVS repository, exercising the cycle splitter: We
-- have three files and some pretty nasty cyclic dependencies:
--
--    fileA:  A -> B -> C
--    file2:       B -> C -> A
--    file3:            C -> A -> B

-- import into monotone and check presence of files
check(mtn("--branch=test", "cvs_import", "--debug", "cvs-repository/test"), 0, false, false)

