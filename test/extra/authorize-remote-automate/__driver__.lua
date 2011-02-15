includecommon("netsync.lua")

mtn_setup()

mkdir("hooks.d")
check(copy(srcdir.."/../extra/mtn-hooks/authorize_remote_automate.lua",
	   "hooks.d/authorize_remote_automate.lua"))

netsync.setup()
append("netsync.lua", "\
\
includedirpattern(get_confdir() .. \"/hooks.d\",\"*.conf\")\
includedirpattern(get_confdir() .. \"/hooks.d\",\"*.lua\")\
")

-- We serve test2.db in all the tests.  However, the monotone server needs
-- to be restarted for every individual test since we're changing the
-- configuration between them.

-- Try with no permissions.  This should fail.
srv = netsync.start(2)
check(mtn("automate","remote","--remote-stdio-host",srv.address,
	  "interface_version"), 1, false, false)
srv:stop()

-- Try with 'interface_version' declared safe for anonymous use.
writefile("hooks.d/authorize_remote_automate.conf",
	  "ARA_safe_commands = { \"interface_version\" }\n")
srv = netsync.start(2)
check(mtn("automate","remote","--remote-stdio-host",srv.address,
	  "interface_version"), 0, true, false)
check(qgrep("^[0-9]+\\.[0-9]+$", "stdout"))
srv:stop()
remove("hooks.d/authorize_remote_automate.conf")

-- Try with permissions for the normal key
writefile("remote-automate-permissions","tester@test.net\n")
srv = netsync.start(2)
check(mtn("automate","remote","--remote-stdio-host",srv.address,
	  "interface_version"), 0, true, false)
check(qgrep("^[0-9]+\\.[0-9]+$", "stdout"))
srv:stop()
