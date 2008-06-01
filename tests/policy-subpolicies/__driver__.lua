
mtn_setup()

check(mtn("create_project", "test_project"), 0, false, false)

check(mtn("create_subpolicy", "test_project.subproject"), 0, false, false)

check(mtn("create_subpolicy", "test_project.subproject.subsub"), 0, false, false)


check(mtn("checkout", "checkout", "--branch=test_project.subproject.__policy__"), 0)

check(exists("checkout/delegations/subsub"))