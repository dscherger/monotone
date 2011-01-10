-- db check checks branch_leaves table

-- a bug in mtn 0.46 caused extra entries to be left in the
-- branch_leaves table; this shows that 'db check' identifies that,
-- and 'regenerate_caches' fixes it.

mtn_setup()

-- create two heads on testbranch, one head on otherbranch
addfile("file1", "rev_A")
commit("testbranch", "rev_A")
base = base_revision()

writefile("file1", "rev_a")
commit("testbranch", "rev_a")
rev_a = base_revision()

revert_to(base)

addfile("file2", "rev_b")
commit("testbranch", "rev_b")
rev_b = base_revision()

writefile("file2", "rev_c")
commit("otherbranch", "rev_c")
rev_c = base_revision()

-- db should be ok
check(mtn("db", "check"), 0, false, false)

-- branch names are stored as binary blobs in the database;
-- these values are from sqlite3 test.db .dump branch_leaves.
testbranch_name = "X'746573746272616E6368'"
otherbranch_name = "X'6F746865726272616E6368'"

-- add an extra head on 'testbranch' in branch_leaves
check(mtn("db", "execute", "INSERT INTO branch_leaves (branch, revision_id) VALUES (" .. testbranch_name .. ", x'" .. base .. "');"), false, false)

-- delete 'otherbranch' from branch_leaves
check(mtn("db", "execute", "DELETE FROM branch_leaves WHERE branch=" .. otherbranch_name .. ";"), false, false)

-- add an extra branch in branch_leaves; it doesn't matter that the
-- name is not actually binary
check(mtn("db", "execute", "INSERT INTO branch_leaves (branch, revision_id) VALUES ('bogusbranch', x'" .. base .. "');"), false, false)

-- db is not ok
check(mtn("db", "check"), 1, false, true)
check(qgrep("cached branch 'bogusbranch' not used", "stderr"))
check(qgrep("branch 'otherbranch' not cached", "stderr"))
check(qgrep("branch 'testbranch' wrong head count", "stderr"))

-- fix it
check(mtn("db", "regenerate_caches"), 0, false, false)

check(mtn("db", "check"), 0, false, false)

-- double check
check(mtn("automate", "heads", "testbranch"), 0, true, false)
check(readfile("stdout") == rev_a .. "\n" .. rev_b .. "\n")

check(mtn("automate", "heads", "otherbranch"), 0, true, false)
check(readfile("stdout") == rev_c .. "\n")

check(mtn("automate", "heads", "bogusbranch"), 0, true, false)
check(readfile("stdout") == "")

-- end of file
