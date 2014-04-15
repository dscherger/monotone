skip_if(not existsonpath("python"))

includecommon("netsync.lua")

mtn_setup()
netsync.setup()
append("netsync.lua", "\n\
\n\
includedirpattern(get_confdir() .. \"/hooks.d\",\"*.conf\")\n\
includedirpattern(get_confdir() .. \"/hooks.d\",\"*.lua\")\n\
")

mkdir("hooks.d")
mkdir("scripts")
check(copy(srcdir.."/../extra/mtn-hooks/monotone-ciabot.lua",
	   "hooks.d/monotone-ciabot.lua"))
check(copy(srcdir.."/../extra/mtn-hooks/monotone-ciabot.py",
	   "scripts/monotone-ciabot.py"))

-- To make life easier, we run monotone-ciabot.py via the script
-- run-monotone-ciabot, which makes sure the output ends up in a
-- known file.
check(get("scripts/run-monotone-ciabot"))

-- Get+write the ciabot configuration
check(get("ciabot.conf"))
writefile("hooks.d/monotone-ciabot.conf",
	  "ciabot_python_script = \"" .. test.root .. "/scripts/run-monotone-ciabot\"\n")

-- A file with expected result
check(get("monotone-ciabot.expected"))

-- Serve test2.db
srv = netsync.start(2)

-- Add some revisions to push
addfile("test1", "foo")
addfile("test2", "bar")
commit()
writefile("test2", "foobar")
commit()

-- Do the transfer
srv:push("testbranch",1)

srv:stop()

check(exists("monotone-ciabot.err"))
check(fsize("monotone-ciabot.err") == 0)
check(exists("monotone-ciabot.out"))
check(samefile("monotone-ciabot.out",
	       "monotone-ciabot.expected"))
