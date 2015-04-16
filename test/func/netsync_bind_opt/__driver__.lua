skip_if(not existsonpath("netstat"))

includecommon("netsync.lua")

-- Heuristic check trying to figure if netstat works. A grsecurity
-- enabled kernel may restrict access to /proc/net, therefore render
-- netstat useless, i.e. it shows no entries.
check({"netstat", "-a", "-n"}, false, true, false)
skip_if(not qgrep("tcp", "stdout") and not readable("/proc/net/tcp"))

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

