
mtn_setup()

check(get("cvs-repository"))

-- A carefully handcrafted CVS repository, exercising the cycle splitter: We
-- have two commits and a branch start in between. The silly thing being,
-- that the ordering is reversed, so we end up with a cycle involving those
-- two commits as well as the branch start.
--
-- For additional cruelty, all commits have equal timestamps.

-- import into monotone and check presence of files
check(mtn("--branch=test", "cvs_import", "--debug", "cvs-repository/test"), 0, false, false)

