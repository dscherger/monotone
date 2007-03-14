
mtn_setup()

check(get("cvs-repository"))

check(mtn("--branch=test", "cvs_import", "--debug", "cvs-repository"), 0, false, false)

check(mtn("list", "branches"), 0, true, false)
check(samelines("stdout", {"test", "test.BRANCH_FROM_UNNAMED_BRANCH_A", "test.BRANCH_FROM_UNNAMED_BRANCH_B"}))

check(mtn("--branch=test.BRANCH_FROM_UNNAMED_BRANCH", "co", "mtnco"))
check(indir("mtnco", mtn("list", "known")), 0, true)
check(samelines("stdout", {"fileA"}))
check(samelines("mtnco/fileA", {"revision 3 of fileA"}))
