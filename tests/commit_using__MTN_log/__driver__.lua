
mtn_setup()

check(get("commit_cancelled.lua"))
check(get("commit_confirmed.lua"))

writefile("_MTN/log", "Log Entry")
writefile("input.txt", "version 0 of the file")

check(mtn("add", "input.txt"), 0, false, false)

-- this should now fail, given that the log file has content and the cancel line
-- has been removed
check(mtn("--branch=testbranch", "--rcfile=commit_cancelled.lua", "commit"), 1, false, true)
check(qgrep('Commit cancelled.', "stderr"))

check(exists("_MTN/log"))
check(fsize("_MTN/log") > 0)

-- this should pass, as the lua hook now returns a string that still includes the
-- cancel line
check(mtn("--branch=testbranch", "--rcfile=commit_confirmed.lua", "commit"), 0, false, false)

tsha = base_revision()
check(exists("_MTN/log"))
check(fsize("_MTN/log") == 0)
check(mtn("ls", "certs", tsha), 0, true, false)
check(qgrep("Log Entry", "stdout"))
