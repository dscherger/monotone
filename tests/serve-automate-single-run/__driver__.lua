include("common/netsync.lua")

mtn_setup()
netsync.setup()

addfile("foo", "bar")
commit()
R1 = base_revision()

server = netsync.start()

check(mtn2("automate", "remote", "--remote-stdio-host", server.address,
    "interface_version"), 0, true, false)
check(qgrep("^0:2:l:", "stdout"))

server:stop()

check(mtn2("automate", "stdio"), 0, true, false, "l6:leavese")
check(qgrep("^0:0:l:0:", "stdout"))

writefile("allow-automate.lua",
          "function get_remote_automate_permitted(x, y, z) return true end")

server = netsync.start({"--rcfile=allow-automate.lua"})

check(mtn2("automate", "remote", "--remote-stdio-host", server.address,
    "interface_version"), 0, true, false)
check(qgrep("^0:0:l:", "stdout"))

check(mtn2("automate", "remote", "--remote-stdio-host", server.address,
    "leaves"), 0, true, false)
check(qgrep("^0:0:l:", "stdout"))

check(mtn2("automate", "remote", "--remote-stdio-host", server.address,
    "stdio"), 0, true, false)
check(qgrep("can't be run", "stdout"))

-- won't work, --revision is no option of automate remote
check(mtn2("automate", "remote", "--remote-stdio-host", server.address,
    "get_file_of", "-r", R1, "foo"), 1, false, false)

-- this doesn't work either because we can't use the option machinery
-- to distinguish valid from invalid options on the _server_, so we expect
-- all options arguments to be directly written after the option
check(mtn2("automate", "remote", "--remote-stdio-host", server.address,
    "get_file_of", "--", "-r", R1, "foo"), 0, true, false)
check(qgrep("wrong argument count", "stdout"))

-- finally this should work
check(mtn2("automate", "remote", "--remote-stdio-host", server.address,
    "get_file_of", "--", "-r".. R1, "foo"), 0, true, false)
check(qgrep("bar", "stdout"))

server:stop()
