skip_if(not existsonpath("test"))
mtn_setup()

addfile("readme.txt", "nothing to see here\n")
check(mtn("ci", "--message=test", "--branch=testbranch"), 0, false, false)

check({"touch", "test_export"}, 0, false, false)
--no args
check(mtn("export"), 2, false, false)

--db, but no revision
check(mtn("export", "--db=test.db"), 2, false, false)

--db and revision, but no dest dir
check(mtn("export", "--db=test.db", "--branch=testbranch"), 2, false, false)

--proper args, but checkout to . should fail
check(mtn("export", "--db=test.db", "--branch=testbranch", "."), 1, false, false)

--db and revision, but test_export is a file!
check(mtn("export", "--db=test.db", "--branch=testbranch", "test_export"), 1, false, false)

--proper args, should work
check({"rm", "test_export"}, 0, false, false)
check(mtn("export", "--db=test.db", "--branch=testbranch", "test_export"), 0, false, false)
check({"test", "-d", "test_export"}, 0, false, false)
check({"test", "-f", "test_export/readme.txt"}, 0, false, false)

--now, the dest exists but is a directory.  fail still
check(mtn("export", "--db=test.db", "-r h:testbranch", "test_export"), 1, false, false)
--there shouldn't be a backup in this case
check({"ls", "-1d", "test_export*bak*"}, 2, false, false)

--verify that the backup directory doesn't exist yet...
check({"ls"}, 0, true, false)
check(not qgrep("test_export.bak", "stdout"))

--do my bidding! (--force)
check(mtn("export", "--db=test.db", "--branch=testbranch", "--force", "test_export"), 0, false, false)
--verify that the backup directory was created.
check({"ls"}, 0, true, false)
check(qgrep("test_export.bak", "stdout"))
