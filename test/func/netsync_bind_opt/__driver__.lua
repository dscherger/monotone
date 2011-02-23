skip_if(not existsonpath("netstat"))

includecommon("netsync.lua")
mtn_setup()
netsync.setup()

math.randomseed(get_pid())
local port = math.random(1024, 65535)

-- test with host:port
srv = netsync.start({"--bind", "localhost:" .. port})
check({"netstat", "-a", "-n"}, 0, true, false)
check(qgrep("127[.]0[.]0[.]1[.:]" .. port, "stdout"))
srv:stop()

-- test with ip:port
srv = netsync.start({"--bind", "127.0.0.1:" .. port})
check({"netstat", "-a", "-n"}, 0, true, false)
check(qgrep("127[.]0[.]0[.]1[.:]" .. port, "stdout"))
srv:stop()

-- test only with :port
srv = netsync.start({"--bind", ":" .. port})
check({"netstat", "-a", "-n"}, 0, true, false)
check(qgrep("([*]|0[.]0[.]0[.]0)[.:]" .. port, "stdout"))
srv:stop()

