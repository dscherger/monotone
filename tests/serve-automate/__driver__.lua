include("common/netsync.lua")
include("common/automate_stdio.lua")

mtn_setup()
netsync.setup()

addfile("foo", "bar")
commit()

writefile("deny-automate.lua",
          "function get_remote_automate_permitted(x, y, z) return false end")

server = netsync.start({"--rcfile=deny-automate.lua"})

local errors = run_remote_stdio(server, "l17:interface_versione", 1, 0, "e")
check(
    table.maxn(errors) == 1 and
    errors[1] == "misuse: Sorry, you aren't allowed to do that."
)

server:stop()

check(mtn2("automate", "stdio"), 0, true, false, "l6:leavese")
check(qgrep("0:l:1:0$", "stdout"))

writefile("allow-automate.lua",
          "function get_remote_automate_permitted(x, y, z) return true end")

server = netsync.start({"--rcfile=allow-automate.lua"})

check(
    nil ~=
    tonumber(run_remote_stdio(server, "l17:interface_versione", 0, 0, "m"))
)

check(
    nil ~=
    tonumber(run_remote_stdio(server, "l17:interface_versionel6:leavese", 0, 0, "m"))
)

check(
    41 ==
    string.len(run_remote_stdio(server, "l17:interface_versionel6:leavese", 0, 1, "m"))
)

local errors = run_remote_stdio(server, "l5:stdioe", 1, 0, "e")
check(
    table.maxn(errors) == 1 and
    errors[1] == "error: sorry, that can't be run remotely or over stdio"
)

server:stop()
