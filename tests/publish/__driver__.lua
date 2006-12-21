skip_if(not existsonpath("test"))
mtn_setup()

addfile("readme.txt", "nothing to see here\n")
check(mtn("ci", "--message=test", "--branch=testbranch"), 0, false, false)

check({"touch", "test_publish"}, 0, false, false)
--no args
check(mtn("publish"), 2, false, false)

--db, but no revision
check(mtn("publish", "--db=test.db"), 2, false, false)

--db and revision, but no dest dir
check(mtn("publish", "--db=test.db", "--branch=testbranch"), 2, false, false)

--proper args, but checkout to . should fail
check(mtn("publish", "--db=test.db", "--branch=testbranch", "."), 1, false, false)

--db and revision, but test_publish is a file!
check(mtn("publish", "--db=test.db", "--branch=testbranch", "test_publish"), 1, false, false)

--proper args, should work
check({"rm", "test_publish"}, 0, false, false)
check(mtn("publish", "--db=test.db", "--branch=testbranch", "test_publish"), 0, false, false)
check({"test", "-d", "test_publish"}, 0, false, false)
check({"test", "-f", "test_publish/readme.txt"}, 0, false, false)

--now, the dest exists but is a directory.  fail still
check(mtn("publish", "--db=test.db", "-r h:testbranch", "test_publish"), 1, false, false)
--there shouldn't be a backup in this case
check({"ls", "-1d", "test_publish*bak*"}, 2, false, false)

--verify that the backup directory doesn't exist yet...
check({"ls"}, 0, true, false)
check(not qgrep("test_publish.bak", "stdout"))

--do my bidding! (--force)
check(mtn("publish", "--db=test.db", "--branch=testbranch", "--force", "test_publish"), 0, false, false)
--verify that the backup directory was created.
check({"ls"}, 0, true, false)
check(qgrep("test_publish.bak", "stdout"))
