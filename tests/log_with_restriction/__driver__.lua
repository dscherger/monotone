-- This test checks that 'mtn log' of a file only shows 
-- only revisions containing that file.

mtn_setup()

mkdir("1")
mkdir("1/2")
mkdir("1/2/3")
mkdir("1/2/3/4")

check(mtn("add", "."), 0, true, false)
commit("testbranch")
rev_root = base_revision()

check(mtn("add", "1"), 0, true, false)
commit("testbranch")

check(mtn("add", "1/2"), 0, true, false)
commit("testbranch")

check(mtn("add", "1/2/3"), 0, true, false)
commit("testbranch")

check(mtn("add", "1/2/3/4"), 0, true, false)
commit("testbranch")

-- the project tree now looks like this
-- .          root depth=0 added in 1st commit
-- ./1        dir  depth=1 added in 2nd commit
-- ./1/2      dir  depth=2 added in 3rd commit
-- ./1/2/3    dir  depth=3 added in 4th commit
-- ./1/2/3/4  dir  depth=4 added in 5th commit

-- include commit 1
check(mtn("log", "--no-graph", "--brief", "--depth", "0", "."), 0, true, false)
check(numlines("stdout") == 1)

-- include commits 1,2
check(mtn("log", "--no-graph", "--brief", "--depth", "1", "."), 0, true, false)
check(numlines("stdout") == 2)

-- include commits 1,2,3
check(mtn("log", "--no-graph", "--brief", "--depth", "2", "."), 0, true, false)
check(numlines("stdout") == 3)

-- include commits 1,2,3,4
check(mtn("log", "--no-graph", "--brief", "--depth", "3", "."), 0, true, false)
check(numlines("stdout") == 4)

-- include commits 1,2,3,4,5
check(mtn("log", "--no-graph", "--brief", "--depth", "4", "."), 0, true, false)
check(numlines("stdout") == 5)


-- exclude commit 1
check(mtn("log", "--no-graph", "--brief", "--depth", "0", "--exclude", "."), 0, true, false)
check(numlines("stdout") == 4)

-- eclude commits 1,2
check(mtn("log", "--no-graph", "--brief", "--depth", "1", "--exclude", "."), 0, true, false)
check(numlines("stdout") == 3)

-- exclude commits 1,2,3
check(mtn("log", "--no-graph", "--brief", "--depth", "2", "--exclude", "."), 0, true, false)
check(numlines("stdout") == 2)

-- exclude commits 1,2,3,4
check(mtn("log", "--no-graph", "--brief", "--depth", "3", "--exclude", "."), 0, true, false)
check(numlines("stdout") == 1)

-- exclude commits 1,2,3,4,5
check(mtn("log", "--no-graph", "--brief", "--depth", "4", "--exclude", "."), 0, true, false)
check(numlines("stdout") == 0)



addfile("early", "early")
commit("testbranch", "Addition of an early file.")

addfile("foo", "foo")
addfile("bar", "bar")
commit("testbranch", "Addition of foo and bar.")
rev_foo1 = base_revision()

writefile("bar", "changed bar")
commit("testbranch", "bar has changed.")

writefile("foo", "changed foo once")
commit("testbranch", "foo has changed once.")
rev_foo2 = base_revision()

check(mtn("drop", "--bookkeep-only", "bar"), 0, false, false)
commit("testbranch", "Dropped bar.")

writefile("foo", "changed foo again")
commit("testbranch", "foo has changed again.")
rev_foo3 = base_revision()

check(mtn("log", "--no-graph", "foo"), 0, true, false)
rename("stdout", "log")

-- note that we also get the initial commit that created the project's root
-- directory here because it is implicitly included as the parent of foo

check(grep("^Revision:", "log"), 0, true, false)
rename("stdout", "revs")
check(numlines("revs") == 4)

check(grep("^Revision: " .. rev_root, "log"), 0, true)
check(grep("^Revision: " .. rev_foo1, "log"), 0, true)
check(grep("^Revision: " .. rev_foo2, "log"), 0, true)
check(grep("^Revision: " .. rev_foo3, "log"), 0, true)
