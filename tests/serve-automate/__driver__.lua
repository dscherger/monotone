-- this test uses netcat
skip_if(not existsonpath("nc"))

include("common/netsync.lua")

mtn_setup()
netsync.setup()

automate_port = math.random(1024, 65535)
server = netsync.start({"--bind-automate", "localhost:" .. automate_port})

check({"nc", "-q", "10", "localhost", automate_port}, 0, true, false,
      "l17:interface_versione")

rename("stdout", "version")

check(qgrep("^0:0:l:", "version"))

server:stop()