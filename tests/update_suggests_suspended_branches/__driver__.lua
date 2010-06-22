-- this is an accompanying xfailing test for #29843

mtn_setup()

addfile("testfile", "blah blah")
commit("base")
base = base_revision()

writefile("testfile", "other other")
commit("incorrect-follower")
child = base_revision()

check(mtn("suspend", "-b", "incorrect-follower", child), 0, false, false)
check(mtn("approve", "-b", "correct-follower", child), 0, false, false)

check(mtn("update", "-r", base), 0, false, false)

-- this should directly update the workspace to the child
-- revision and not ask us which branch to choose
xfail(mtn("update", "-r", child), 0, false, false)
