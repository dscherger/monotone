include("common/netsync.lua")
mtn_setup()
netsync.setup()
rseed = get_pid()

addfile("testfile", "foo")
commit()
t1 = base_revision()

-- pull with key in db 2
math.randomseed(rseed)
netsync.pull("testbranch")
check(not qgrep("anonymous","ts-stderr"))
check(mtn2("automate", "get_revision", t1), 0, false, false)

addfile("testfile", "bar")
commit()
t2 = base_revision()

-- pull anonymously in db 2
math.randomseed(rseed)
netsync.pull({"testbranch","--anonymous"})
check(qgrep("anonymous","ts-stderr"))
check(mtn2("automate", "get_revision", t2), 0, false, false)
