
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

