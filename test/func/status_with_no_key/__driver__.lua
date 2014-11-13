mtn_setup()

-- Since the status revamp, this test is not as relevant as it used to
-- be, because 'mtn status' doesn't output the would-be-committer,
-- anymore.

-- normal, one specified key
check(mtn("status"), 0, true, false)
check(not qgrep("tester@test.net", "stdout"))

-- explicitly choose no key
check(safe_mtn("status", "-k", ""), 0, true, false)
check(not qgrep("tester@test.net", "stdout"))

-- no key specified, with one available
check(remove("_MTN"))
check(safe_mtn("setup", "--branch=testbranch", "."), 0, false, false)
check(safe_mtn("status"), 0, true, false)
check(not qgrep("tester@test.net", "stdout"))

-- no key specified, with multiple available
check(mtn("genkey", "other@test.net"), 0, false, false, string.rep("other@test.net\n", 2))
check(remove("_MTN"))
check(safe_mtn("setup", "--branch=testbranch", "."), 0, false, false)
check(safe_mtn("status"), 0, true, false)
check(not qgrep("tester@test.net", "stdout"))
check(not qgrep("other@test.net", "stdout"))
