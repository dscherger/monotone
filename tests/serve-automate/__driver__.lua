include("common/netsync.lua")

mtn_setup()
netsync.setup()

addfile("foo", "bar")
commit()

server = netsync.start()

check(mtn2("automate", "remote_stdio", server.address), 0, true, false,
      "l17:interface_versione")
check(qgrep("^0:1:e:45:misuse: Sorry, you aren't allowed to do that.0:1:l:0:", "stdout"))

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

check(mtn2("automate", "remote_stdio", server.address), 0, true, false,
      "l5:stdioe")
check(qgrep("can't be run", "stdout"))

server:stop()
