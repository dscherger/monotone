
mtn_setup()

check(get("cvs-repository"))

check(mtn("--branch=test", "cvs_import", "--debug", "cvs-repository"), 0, false, false)

check(mtn("list", "branches"), 0, true, false)
check(samelines("stdout", {"test", "test.BRANCH_FROM_UNNAMED_BRANCH_A", "test.BRANCH_FROM_UNNAMED_BRANCH_B", "test.UNNAMED_BRANCH_1", "test.UNNAMED_BRANCH_2"}))

check(mtn("--branch=test.UNNAMED_BRANCH_1", "co", "mtnco1"))
check(indir("mtnco1", mtn("list", "known")), 0, true)
check(samelines("stdout", {"fileA"}))
check(samelines("mtnco1/fileA", {"revision 2 of fileA"}))

check(mtn("--branch=test.UNNAMED_BRANCH_2", "co", "mtnco2"))
check(indir("mtnco2", mtn("list", "known")), 0, true)
check(samelines("stdout", {"fileA"}))
check(samelines("mtnco2/fileA", {"revision 4 of fileA"}))
