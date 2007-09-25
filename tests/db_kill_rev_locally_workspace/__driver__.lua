--
-- These tests ensure that a workspace which is affected by a revision
-- removal is gracefully handled
--

mtn_setup()

addfile("first", "first")
commit()
first = base_revision()

check(mtn("rename", "first", "first-renamed"), 0, false, false)
addfile("second", "second")
commit()
second = base_revision()

-- check that the revision is properly recreated if the revision
-- is killed for real...
check(mtn("db", "kill_rev_locally", second), 0, false, false)
check(base_revision() == first)
-- ...and if the changes can be recommitted so the same revision is created
commit()
check(base_revision() == second)

-- make sure it works if there are local changes, too
addfile("third", "third")
check(mtn("db", "kill_rev_locally", second), 0, false, false)
check(base_revision() == first)
check(mtn("automate", "get_revision"), 0, true)
check(qgrep("add_file \"second\"", "stdout"))
check(qgrep("add_file \"third\"", "stdout"))
commit()
third = base_revision()

-- ensure that the workspace is not touched if we kill
-- a revision which is not the base of the current workspace
check(mtn("update", "-r", first), 0, false, false)
addfile("fourth", "fourth")
check(mtn("db", "kill_rev_locally", third), 0, false, false)
check(base_revision() == first)
commit()
fourth = base_revision()

-- ensure that kill_rev_locally errors out if the current workspace is
-- a merge of the rev-to-be-killed with something else
revert_to(first)
addfile("fifth", "fifth")
commit()
fifth = base_revision()
check(mtn("merge_into_workspace", fourth), 0, false, false)

check(mtn("db", "kill_rev_locally", fourth), 1, false, false)
check(mtn("db", "kill_rev_locally", fifth), 1, false, false)
