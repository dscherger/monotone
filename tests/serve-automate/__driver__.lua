include("common/netsync.lua")

mtn_setup()
netsync.setup()

addfile("foo", "bar")
commit()

server = netsync.start()

check(mtn2("automate", "remote_stdio", server.address), 0, true, false,
      "l17:interface_versione")
check(qgrep("^0:2:l:", "stdout"))

server:stop()

check(mtn2("automate", "stdio"), 0, true, false, "l6:leavese")
check(qgrep("^0:0:l:0:", "stdout"))

writefile("allow-automate.lua",
          "function get_remote_automate_permitted(x, y, z) return true end")

server = netsync.start({"--rcfile=allow-automate.lua"})

check(mtn2("automate", "remote_stdio", server.address), 0, true, false,
      "l17:interface_versione")
check(qgrep("^0:0:l:", "stdout"))

check(mtn2("automate", "remote_stdio", server.address), 0, true, false,
      "l17:interface_versionel6:leavese")
check(qgrep("^0:0:l:", "stdout"))
check(qgrep("^1:0:l:41:", "stdout"))

server:stop()
