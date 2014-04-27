includecommon("netsync.lua")

mtn_setup()
netsync.setup()

addfile("foo", "bar")
commit()
R1 = base_revision()

writefile("deny-automate.lua",
          "function get_remote_automate_permitted(x, y, z) return false end")

server = netsync.start({"--rcfile=deny-automate.lua"})

check(mtn2("automate", "remote", "--remote-stdio-host", server.address,
    "interface_version"), 1, true, true)
check(qgrep("you aren't allowed to do that", "stderr"))

server:stop()

check(mtn2("automate", "stdio"), 0, true, nil, "l6:leavese")
check(qgrep("[[:xdigit:]]{40}", "stderr"))

writefile("allow-automate.lua",
          "function get_remote_automate_permitted(x, y, z) return true end")

server = netsync.start({"--rcfile=allow-automate.lua"})

check(mtn2("automate", "remote", "--remote-stdio-host", server.address,
    "interface_version"), 0, true, false)
check(qgrep("^[0-9]{2,}\\.[0-9]+$", "stdout"))

check(mtn2("automate", "remote", "--remote-stdio-host", server.address,
    "leaves"), 0, true, false)
check(qgrep("[[:xdigit:]]{40}", "stderr"))

check(mtn2("automate", "remote", "--remote-stdio-host", server.address,
    "stdio"), 1, true, true)
check(qgrep("can't be run", "stderr"))

-- won't work, --revision is no option of automate remote
check(mtn2("automate", "remote", "--remote-stdio-host", server.address,
    "get_file_of", "-r", R1, "foo"), 1, false, false)

-- this doesn't work either because we can't use the option machinery
-- to distinguish valid from invalid options on the _server_, so we expect
-- all options arguments to be directly written after the option
check(mtn2("automate", "remote", "--remote-stdio-host", server.address,
    "get_file_of", "--", "-r", R1, "foo"), 1, true, true)
check(qgrep("wrong argument count", "stderr"))

-- finally this should work
check(mtn2("automate", "remote", "--remote-stdio-host", server.address,
    "get_file_of", "--", "-r".. R1, "foo"), 0, true, false)
check(qgrep("bar", "stdout"))

server:stop()

if ostype ~= "Windows" then
-- 'file:' not supported on Windows

copy("allow-automate.lua", "custom_test_hooks.lua")
test_uri="file://" .. url_encode_path(test.root .. "/test.db")
check(mtn2("automate", "remote", "--remote-stdio-host",
	   test_uri,
	   "get_file_of", "--", "-r".. R1, "foo"), 0, true, false)
check(qgrep("bar", "stdout"))
end
