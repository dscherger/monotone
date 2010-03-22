-- very basic policy branch test
mtn_setup()

check(mtn("genkey", "other@test.net"), 0, false, false,
      "other@test.net\nother@test.net\n")

check(mtn("create_project", "test_project"), 0, false, false)

check(mtn("ls", "branches"), 0, true, false)
check(not qgrep("test_project.__policy__", "stdout"))

check(mtn("checkout", "checkout", "--branch=test_project.__policy__"), 0)

chdir("checkout")

base=base_revision()

addfile("file1", "data1")
check(mtn("ci", "-mgood", "--branch=test_project.__policy__"), 0, false, false)

check(mtn("update", "-r", base), 0, false, false)
addfile("file2", "data2")
check(mtn("commit", "-mbad", "--key=other@test.net",
	  "--branch=test_project.__policy__"), 0, false, false)

-- the second commit should be ignored
check(mtn("merge", "--branch=test_project.__policy__"), 0, false, true)
check(qgrep("already merged", "stderr"))

check(mtn("update", "-r", base), 0, false, false)
addfile("file3", "data3")
-- This will actually fail after the commit proper if --policy-revision
-- isn't given, because you can't get the new head count because the
-- policy is suddenly invalid...
check(mtn("ci", "-mtwogood", "--branch=test_project.__policy__",
          "--policy-revision=test_project@" .. base), 0, false, false)

-- Can't do stuff now, because the policy branch has two heads.
check(mtn("heads", "--branch=test_project.__policy__"), 1, false, false)
check(mtn("merge", "--branch=test_project.__policy__"), 1, false, false)

-- check that we can recover from this
check(mtn("merge", "--branch=test_project.__policy__",
	  "--policy-revision=test_project@" .. base), 0, false, false)

check(mtn("update", "-r", base), 0, false, false)
check(mtn("up"), 0, false, false)

check(base ~= base_revision())

-- check that we can delegate using a revision id
mkdir("delegations")
addfile("delegations/tp", "revision_id [" .. base .. "]");
check(mtn("ci", "-mx", "--branch=test_project.__policy__"), 0, false, false)

check(mtn("heads", "--branch=test_project.tp.__policy__"), 0, true, false)
check(qgrep(base_revision(), "stdout"), 0, false, false)
