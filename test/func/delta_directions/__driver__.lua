include("/common/netsync.lua")
mtn_setup()
netsync.setup()

check(mtn2("set", "database", "delta-direction", "both"), 0)
check(mtn3("set", "database", "delta-direction", "forward"), 0)


addfile("foo", "bar")
commit()

srv = netsync.start()
srv:pull("*", 2)
srv:stop()

srv = netsync.start(3)
srv:push("*", 2)
srv:stop()

----------------------------------
addfile("uvw", "xyz")
writefile("foo", "baz")
check(mtn2("commit", "-mx"), 0, false, false)

srv = netsync.start(3)
srv:push("*", 2)
srv:stop()

srv = netsync.start(2)
srv:pull("*", 1)
srv:stop()

----------------------------------
addfile("abc", "def")
writefile("uvw", "rst")
writefile("foo", "fnord")
check(mtn3("commit", "-mx"), 0, false, false)

srv = netsync.start(3)
srv:pull("*", 2)
srv:stop()

srv = netsync.start()
srv:push("*", 2)
srv:stop()

----------------------------------------------------
check(mtn("db", "info"), 0, true)
rename("stdout", "info-1")
check(mtn2("db", "info"), 0, true)
rename("stdout", "info-2")
check(mtn3("db", "info"), 0, true)
rename("stdout", "info-3")

check(qgrep("revisions *: *3$", "info-1"))
check(qgrep("revisions *: *3$", "info-2"))
check(qgrep("revisions *: *3$", "info-3"))

check(qgrep("file deltas *: *3$", "info-1"))
check(qgrep("file deltas *: *6$", "info-2"))
check(qgrep("file deltas *: *3$", "info-3"))