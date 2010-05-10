-- test that 'mtn cert' does not require a branch option, even when
-- there is no branch cert on the revision.
--
-- bug# 7221

mtn_setup()

addfile("base", "base")
commit("testbranch")
rev = base_revision()

-- delete the branch cert from the revision
check(mtn("db", "kill_branch_certs_locally", "testbranch"), 0, false, false)

-- ensure we don't find a branch option
remove("_MTN")

check(mtn("cert", rev, "branch", "anotherbranch"), 0, false, false)

-- end of file
