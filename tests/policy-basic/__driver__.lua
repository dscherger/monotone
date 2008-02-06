-- very basic policy branch test
mtn_setup()

check(mtn("genkey", "other@test.net"), 0, false, false,
      "other@test.net\nother@test.net\n")

check(mtn("create_project", "test_project"), 0, false, false)

check(mtn("ls", "branches"), 0, true)
check(qgrep("test_project.__policy__", "stdout"), 0)

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
check(mtn("ci", "-mtwogood", "--branch=test_project.__policy__"), 0, false, false)

-- Can't do stuff now, because the policy branch has two heads.
check(mtn("heads", "--branch=test_project.__policy__"), 1, false, false)
check(mtn("merge", "--branch=test_project.__policy__"), 1, false, false)
