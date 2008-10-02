
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

check(mtn("create_project", "test_project"), 0, false, false)
check(mtn("create_branch", "test_project.testbranch"), 0, false, false)

netsync.pull("test_project.*")

addfile("testfile", "file contents")
commit("test_project.testbranch")
rev1 = base_revision()

netsync.pull("test_project.*")
check(mtn2("ls", "certs", rev1), 0, false)

writefile("testfile", "other contents")
check(mtn2("commit", "-mx"), 0, false, false)
rev2 = base_revision()

netsync.push("test_project.*")
check(mtn("ls", "certs", rev2), 0, false)


srv = netsync.start({"--debug"})

-- send an update to the policy branch
check(mtn2("create_branch", "test_project.otherbranch"), 0, false, false)
srv:push("test_project.*", 2)
-- send a revision on the new branch
writefile("testfile", "third contents")
check(mtn2("commit", "-mx", "-b", "test_project.otherbranch"), 0, false, false)
other_rev = base_revision()
srv:push("test_project.*", 2)
-- see if the server recognized the new branch
-- if the policy info is stale, it won't recognize it for the
-- globish expansion
srv:pull("test_project.*", 3)
check(mtn3("ls", "certs", other_rev), 0, false)

srv:stop()
