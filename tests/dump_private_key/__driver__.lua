
mtn_setup()

check(mtn("au", "generate_key", "foo@bar.com", "bla"), 0, false, false)
check(mtn("privkey", "foo@bar.com"), 0, true, false)
check(qgrep("\\[keypair foo@bar.com\\]", "stdout"))

-- check without a database or workspace
check(nodb_mtn("privkey", "--no-workspace", "foo@bar.com"), 0, true, false)
check(qgrep("\\[keypair foo@bar.com\\]", "stdout"))

