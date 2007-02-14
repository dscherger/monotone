
-- very much based on importing_cvs_with_unnamed_branch, but
-- has the branch including all its deltas deleted completely

mtn_setup()

check(get("e"))

check(mtn("--branch=test", "cvs_import", "e"), 1, false, true)
check(samelines("stderr", {"mtn: error: delta for a branchpoint is missing (1.1.2.1)"}))
