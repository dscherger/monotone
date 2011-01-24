mtn_setup()

writefile("file1", "file1")

check(mtn("add", "file1"), 0, false, false)
commit()

-- attempt to export the monotone history with a bad author map

writefile("author.map", "tester@test.net = <tester@test.net>\n")

check(mtn("git_export", "--authors-file", "author.map"), 1, false, true)
check(qgrep("invalid git author", "stderr"))

-- attempt to export the monotone history with a good author map

writefile("author.map", "tester@test.net = Tester <tester@test.net>\n")

check(mtn("git_export", "--authors-file", "author.map"), 0, true, true)
check(not qgrep("invalid git author", "stderr"))
check(qgrep("author Tester <tester@test.net>", "stdout"))
check(qgrep("committer Tester <tester@test.net>", "stdout"))
