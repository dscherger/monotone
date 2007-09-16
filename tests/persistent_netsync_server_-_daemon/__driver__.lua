skip_if(ostype == "Windows")
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

-- without specifying --pid-file, --daemon should fail.
check(mtn("serve", "--daemon"), 1, false, true)
check(qgrep('When using --daemon, you must supply --pid-file also', "stderr"))

--now, fire up a real daemon.
check(mtn("serve", "--daemon", "--pid-file=./pid1"), 0, false, false)
pid = string.gsub(readfile("./pid1"), "[\r\n]+$", "")

--kill the process (if this fails, the pid wasn't
--running in the background anyway)
check({'kill', pid}, 0, false, false)
runcmd({'rm', './pid1'})
