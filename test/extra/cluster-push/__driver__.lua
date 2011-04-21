includecommon("netsync.lua")

mtn_setup()
netsync.setup()
append("netsync.lua", "\
\
includedirpattern(get_confdir() .. \"/hooks.d\",\"*.conf\")\
includedirpattern(get_confdir() .. \"/hooks.d\",\"*.lua\")\
")
mkdir("hooks.d")

-- the server that shall receive the push from the other server
srv1 = netsync.start(2)
sleep(5)

-- the server that shall push to the other server.  We add this
-- hook now, so the former server doesn't catch it
check(copy(srcdir.."/../extra/mtn-hooks/monotone-cluster-push.lua",
	   "hooks.d/monotone-cluster-push.lua"))
writefile("hooks.d/monotone-cluster-push.conf",
	  "MCP_debug = true\nMCP_verbose_startup=true\n")
writefile("cluster-push.rc",
	  "\
pattern \"*\"\
server \""..srv1.address.."\"\n")
srv2 = netsync.start(3)
sleep(5)

-- Move the hook, so we don't confuse the clients
rename("hooks.d/monotone-cluster-push.conf",
       "hooks.d/monotone-cluster-push.conf.disabled")
rename("hooks.d/monotone-cluster-push.lua",
       "hooks.d/monotone-cluster-push.lua.disabled")

-- Now, send something to the second server
addfile("foo", "bar")
commit()
srv2:push("testbranch",1)
pushed_rev=base_revision()
sleep(5)

-- Destroy the client setup and rebuild it, so we can pull frmo scratch
remove("test.db")
remove("_MTN")
mtn_setup()

-- Now, fetch from the first server
srv1:pull("testbranch",1)
check(mtn("checkout", "--branch=testbranch", "checkout"), 0, false, false)
savedir = chdir("checkout")
pulled_rev=base_revision()
chdir(savedir)

-- Check that the revisions are the same
check(pushed_rev == pulled_rev)

-- Cleanup
srv1:stop()
srv2:stop()
