mtn_setup()

writefile("file1", "file1")
check(mtn("add", "file1"), 0, false, false)
check(mtn("commit", "-m", "test1", "--author", "tester1@test.net"), 0, false, true)

writefile("file1", "file1x")
check(mtn("commit", "-m", "test2",  "--author", "<tester2@test.net>"), 0, false, true)

writefile("file1", "file1y")
check(mtn("commit", "-m", "test3",  "--author", "tester3"), 0, false, true)

writefile("file1", "file1z")
check(mtn("commit", "-m", "test4",  "--author", "tester4 <tester@test.net>"), 0, false, true)

check(mtn("log"), 0, true, false)

-- export the monotone history

check(mtn("git_export"), 0, true, true)
check(qgrep("committer tester <tester@test.net>", "stdout"))
check(qgrep("author tester1 <tester1@test.net>", "stdout"))
check(qgrep("author tester2 <tester2@test.net>", "stdout"))
check(qgrep("author tester3 <tester3>", "stdout"))
check(qgrep("author tester4 <tester@test.net>", "stdout"))

-- one more commit with an invalid author

writefile("file1", "file1zz")
check(mtn("commit", "-m", "test4",  "--author", "<tester5@test.net"), 0, false, true)

check(mtn("git_export"), 1, false, true)

