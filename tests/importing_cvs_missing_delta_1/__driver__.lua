
-- very much based on importing_cvs_with_unnamed_branch, but
-- has the branch including all its deltas deleted completely

mtn_setup()

check(get("cvs-repository"))

check(mtn("--branch=test", "cvs_import", "cvs-repository"), 0, false, true)
check(qgrep("delta for RCS version 1.1.2.1 referenced from branch BRANCH_FROM_UNNAMED_BRANCH is missing from file fileA,v", "stderr"))

