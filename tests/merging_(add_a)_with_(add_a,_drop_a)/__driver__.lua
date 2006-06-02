
mtn_setup()

-- We want a graph which looks like:
--              A
--            / | \
--           F  C  G
--           |/ |  |
--           D  E  |
--              \ /
--               B

-- B and D are heads of branch.main and branch.fork respectively, we want to
-- "propagate branch.main branch.fork". 

-- The revs F, C, and D are members of branch.fork. 
-- A, C, E, G, and B are members of branch.main (C is shared)

-- C is "add bar", E is "drop bar", other revisions involve non-conflicting
-- file additions or merges.

writefile("foo", "extra blah blah foo")
writefile("bar", "extra blah blah bar")
writefile("quux", "extra blah blah quux")
writefile("iced", "stuff here")

revs = {}

-- produce state A
check(cmd(mtn("add", "foo")), 0, false, false)
commit("branch.main")
revs.a = base_revision()

-- produce state C
check(cmd(mtn("add", "bar")), 0, false, false)
commit("branch.main")
revs.c = base_revision()
check(cmd(mtn("cert", revs.c, "branch", "branch.fork")))

-- produce state F
revert_to(revs.a)
check(cmd(mtn("add", "iced")), 0, false, false)
commit("branch.fork")
revs.f = base_revision()

-- merge heads of branch.fork to make D
check(cmd(mtn("--branch=branch.fork", "merge")), 0, false, false)

-- produce state E
revert_to(revs.c, "branch.main")
check(cmd(mtn("drop", "bar")), 0, false, false)
commit("branch.main")
revs.e = base_revision()

-- state G
revert_to(revs.a)
check(cmd(mtn("add", "quux")), 0, false, false)
commit("branch.main")
revs.g = base_revision()

-- merge to get state B
check(cmd(mtn("--branch=branch.main", "merge")), 0, false, false)

-- now try the propagate
check(cmd(mtn("propagate", "branch.main", "branch.fork")), 0, false, false)

-- check
remove_recursive("_MTN")
check(cmd(mtn("--branch=branch.fork", "checkout", ".")))

check(cmd(mtn("automate", "get_manifest_of")), 0, true)
rename("stdout", "manifest")
check(not qgrep("bar", "manifest"))
check(qgrep("quux", "manifest"))
check(qgrep("foo", "manifest"))
check(qgrep("iced", "manifest"))
