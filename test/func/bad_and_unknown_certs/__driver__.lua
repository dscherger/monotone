mtn_setup()

addfile("foo", "bar")
commit()
rev_1 = base_revision()

writefile("foo", "foo")
commit()
rev_2 = base_revision()

check(mtn("genkey", "foo@test.net"), 0, false, false, string.rep("foo@test.net\n", 2))
check(mtn("approve", rev_1, "--key=foo@test.net"), 0, false, false)
check(mtn("dropkey", "tester@test.net"), 0, false, false)
check(mtn("heads"), 0, true, true)
check(qgrep(rev_1, "stdout"))
check(qgrep("unknown.*" .. rev_2, "stderr"))
check(not qgrep("unknown.*" .. rev_1, "stderr"))