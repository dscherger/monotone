
-- very much based on importing_cvs_with_unnamed_branch, but
-- has the branch including all its deltas deleted completely

mtn_setup()

check(get("cvs-repository"))

check(mtn("--branch=test", "cvs_import", "cvs-repository"), 1, false, true)
check(samelines("stderr", {
    "mtn: parsing rcs files",
    "mtn: error: delta for a branchpoint is missing (1.1.2.1)"
}))
