
mtn_setup()

getfile("commit_log.lua")

writefile("_MTN/log", "Log entry")

writefile("input.txt", "version 0 of the file")

check(mtn("add", "input.txt"), 0, false, false)

check(mtn("--branch=testbranch", "--rcfile=commit_log.lua", "commit"), 0, false, false)

check(mtn("--branch=testbranch", "co", "testbranch"))

check(exists("testbranch/_MTN/log"))
check(fsize("_MTN/log") == 0)
check(fsize("testbranch/_MTN/log") == 0)
