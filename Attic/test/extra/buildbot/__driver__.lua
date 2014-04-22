skip_if(not existsonpath("buildbot"))

----- Set up a dumping buildbot that reacts directly on the commit.
buildbot1_slave_port = tostring(math.random(1024,65535))
buildbot1_result_file = "buildbot1.results.dat"
mkdir("buildbot1")
check({"buildbot", "create-master", "buildbot1"}, 0, false, false)
get("run-buildbot.sh")		-- We use this to start the buildbot because it sets the needed
				-- environment variables TEST_PORT and TEST_OUTPUT
get("master.cfg", "buildbot1/master.cfg")

----- Set up a dumping buildbot that reacts on the push.
buildbot2_slave_port = tostring(math.random(1024,65535))
buildbot2_result_file = "buildbot2.results.dat"
mkdir("buildbot2")
check({"buildbot", "create-master", "buildbot2"}, 0, false, false)
get("run-buildbot.sh")		-- We use this to start the buildbot because it sets the needed
				-- environment variables TEST_PORT and TEST_OUTPUT
get("master.cfg", "buildbot2/master.cfg")

----- Set up the hooks
mtn_setup()
append("test_hooks.lua", "\n\
\n\
includedirpattern(get_confdir() .. \"/hooks.d\",\"*.conf\")\n\
includedirpattern(get_confdir() .. \"/hooks.d\",\"*.lua\")\n\
")
mkdir("hooks.d")
check(copy(srcdir.."/../extra/mtn-hooks/monotone-buildbot.lua",
	   "hooks.d/monotone-buildbot.lua"))
writefile("hooks.d/monotone-buildbot.conf",
	  "MB_buildbot_master = \"localhost:"..buildbot1_slave_port.."\"\n")

includecommon("netsync.lua")
netsync.setup()
append("netsync.lua", "\n\
\n\
includedirpattern(get_confdir() .. \"/hooks-net.d\",\"*.conf\")\n\
includedirpattern(get_confdir() .. \"/hooks-net.d\",\"*.lua\")\n\
")
mkdir("hooks-net.d")
check(copy(srcdir.."/../extra/mtn-hooks/monotone-buildbot.lua",
	   "hooks-net.d/monotone-buildbot.lua"))
writefile("hooks-net.d/monotone-buildbot.conf",
	  "MB_buildbot_master = \"localhost:"..buildbot2_slave_port.."\"\n")


function protectedrun()
   ----- Start all servers
   check({"./run-buildbot.sh", "buildbot1", buildbot1_slave_port, buildbot1_result_file}, 0, false, false)
   check({"./run-buildbot.sh", "buildbot2", buildbot2_slave_port, buildbot2_result_file}, 0, false, false)
   srv = netsync.start(2)

   ----- Now, just commit something
   addfile("test1", "testing")
   commit()

   ----- And push
   srv:push("testbranch",1)
end

b1,res1 = pcall(protectedrun)

----- Stop all server processes
protectedstops = {
   function ()
      srv:stop()
   end,
   function ()
      check({"buildbot", "stop", "buildbot1"}, 0, false, false)
   end,
   function ()
      check({"buildbot", "stop", "buildbot2"}, 0, false, false)
   end
}

for _,f in pairs(protectedstops) do
   b2,res2 = pcall(f)
end

check(b1)

----- Check all results
get("expected.result.dat")
check(samefile(buildbot1_result_file, "expected.result.dat"), 0, false, false)
check(samefile(buildbot2_result_file, "expected.result.dat"), 0, false, false)
