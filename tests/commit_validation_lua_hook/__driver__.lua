
mtn_setup()

check(get("commit_validate.lua"))

writefile("input.txt", "version 0 of the file")

check(mtn("add", "input.txt"), 0, false, false)
check(get("errmsg"))
check(mtn("--branch=testbranch", "--rcfile=commit_validate.lua", "commit", "-m", "denyme"), 1, false, true)
canonicalize("stderr")
check(grep("-v", "detected at", "stderr"), 0, true)
check(samefile("errmsg", "stdout"))
check(mtn("--branch=testbranch", "--rcfile=commit_validate.lua", "commit", "-m", "allowme"), 0, false, false)
