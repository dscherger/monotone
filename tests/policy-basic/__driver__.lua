-- very basic policy branch test
mtn_setup()

check(mtn("genkey", "other@test.net"), 0, false, false,
      "other@test.net\nother@test.net\n")

check(mtn("create_project", "test_project"), 0, false, false)

check(mtn("ls", "branches"), 0, true, false)
check(not qgrep("test_project.__policy__", "stdout"))

check(mtn("create_branch", "test_project.test_branch"), 0, nil, false)
check(mtn("checkout", "checkout", "--branch=test_project.__policy__"), 0)

chdir("checkout")

addfile("file0", "data0")
check(mtn("ci", "-mbase", "--branch=test_project.__policy__"), 0, false, false)

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

chdir("..")
addfile("branch_file", "datadatadata")
commit("test_project.test_branch", "hackhackhack")
chdir("checkout")

check(mtn("update", "-r", base), 0, false, false)
addfile("file3", "data3")
commit("test_project.__policy__", "twogood")

-- Can't do stuff now, because the policy branch has two heads.
chdir("..")
addfile("otherfile", "otherdata")
check(mtn("ci", "--branch=test_project.test_branch", "-mx"), 1)
check(mtn("heads", "--branch=test_project.test_branch"), 1)

-- but can do stuff with the --policy-revision option
check(mtn("ci", "-mcommit", "--branch=test_project.test_branch",
          "--policy-revision=test_project@" .. base), 0, false, false)

chdir("checkout")

-- check that we can recover from this
check(mtn("merge", "--branch=test_project.__policy__"), 0, false, false)

check(mtn("update", "-r", base), 0, false, false)
check(mtn("up"), 0, false, false)

check(base ~= base_revision())

chdir("..")
addfile("foo", "bar")
commit("test_project.test_branch")
tip = base_revision()

chdir("checkout")

-- check that we can delegate using a revision id
mkdir("delegations")
addfile("delegations/tp", "revision_id [" .. base .. "]");
check(mtn("ci", "-mx", "--branch=test_project.__policy__"), 0, false, false)

check(mtn("heads", "--branch=test_project.tp.test_branch"), 0, true, false)
check(qgrep(tip, "stdout"), 0, false, false)
