include("common/netsync.lua")
mtn_setup()
netsync.setup()


addfile("foo", "bar")
commit("testbranch")
baserev = base_revision()

addfile("aaa", "aaa")
commit("firstbranch")
check(mtn("up", "-r", baserev), 0, false, false)

addfile("bbb", "bbb")
commit("secondbranch")


srv1 = netsync.start()

srv1:pull({"--dry-run", "*"}, 2, 0, true)
check(qgrep("receive 3 revisions, 12 certs, and 1 keys", "stderr"))
check(not qgrep("send", "stderr"))

srv1:pull("{testbranch,firstbranch}", 2)
srv1:pull("{testbranch,secondbranch}", 3)


srv2 = netsync.start(2)

srv2:pull({"--dry-run", "*"}, 3, 0, true)
check(qgrep("receive 1 revisions, 4 certs, and 0 keys", "stderr"))
check(not qgrep("send", "stderr"))

srv2:push({"--dry-run", "*"}, 3, 0, true)
check(qgrep("send 4 certs and 0 keys", "stderr"))
check(qgrep("send 1 revision", "stderr"))
check(qgrep("1 in branch secondbranch", "stderr"))
check(not qgrep("receive", "stderr"))

srv2:sync({"--dry-run", "*"}, 3, 0, true)
check(qgrep("receive 1 revisions, 4 certs, and 0 keys", "stderr"))
check(qgrep("send 4 certs and 0 keys", "stderr"))
check(qgrep("send 1 revision", "stderr"))
check(qgrep("1 in branch secondbranch", "stderr"))

srv2:sync({"*"}, 3, 0, true)
srv2:sync({"--dry-run", "*"}, 3, 0, true)
check(qgrep("receive 0 revisions, 0 certs, and 0 keys", "stderr"))
check(qgrep("send 0 certs and 0 keys", "stderr"))
check(qgrep("send 0 revision", "stderr"))
check(not qgrep("in branch", "stderr"))