skip_if(not existsonpath("mime-construct"))
skip_if(not existsonpath("source-highlight"))

includecommon("netsync.lua")

mtn_setup()
netsync.setup()
append("netsync.lua", "\n\
\n\
includedirpattern(get_confdir() .. \"/hooks.d\",\"*.conf\")\n\
includedirpattern(get_confdir() .. \"/hooks.d\",\"*.lua\")\n\
")

-- To make sure we get things into a file and not sent to some unknown
-- email address (or /dev/null, which doesn't help us much), we do a
-- little bit of a hack to modify the behaviour of mime-construct.  It
-- works as follows:
-- monotone server, on receiving a revision, will call the functions in
-- monotone-mail-notify.lua, as normal.  The functions there will not
-- call the shell script monotone-mail-notify directly, though.  Instead,
-- it will call run-mail-notify (present in this test directory), which
-- will alter $PATH locally to call a local version of mime-construct
-- (basically, a shell script that calls mime-construct with an extra
-- parameter to make sure it outputs to file instead of sending an email).
mkdir("hooks.d")
mkdir("scripts")
check(copy(srcdir.."/../extra/mtn-hooks/monotone-mail-notify.lua",
	   "hooks.d/monotone-mail-notify.lua"))
check(copy(srcdir.."/../extra/mtn-hooks/monotone-mail-notify",
	   "scripts/monotone-mail-notify"))
check(get("scripts/run-mail-notify"))
check(get("scripts/mime-construct"))
check(get("hooks.d/authorize_mail_notify_commands.lua"))

-- A file with expected result and the script that generates it
check(get("mime-construct.expected"))
check(get("scripts/mime-construct-extract"))

-- Serve test2.db
srv = netsync.start(2)

-- Write the notification configuration
mkdir(test.root .. "/spool")
check(writefile("notify", "server \"mtn://" .. srv.address .. "?testbranch\"\
from \"code@example.com\"\
keydir \"" .. test.root .. "/keys\"\
key \"\"\
shellscript \"" .. test.root .. "/scripts/run-mail-notify\"\
base \"" .. test.root .."/spool\"\
\
pattern \"*\"\
allow \"foo@example.com\"\
"))

-- Add some revisions to push
addfile("test1", "foo")
addfile("test2", "bar")
commit()
writefile("test2", "foobar")
commit()

-- Do the transfer
srv:push("testbranch",1)

check(exists("mime-construct.out"))
wait(spawn_redirected("mime-construct.out",
		      "mime-construct.extract",
		      "mime-extract.err",
		      "scripts/mime-construct-extract"))
check(samefile("mime-construct.extract",
	       "mime-construct.expected"))
srv:stop()
