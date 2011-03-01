
mtn_setup()

check(get("test.lua"))
check(mtn("setup", "--rcfile=test.lua", "--branch=testbranch", "subdir"), 0, false, true)
check(qgrep("asdfghjkl", "stderr"))
check(qgrep("qwertyuiop", "stderr"))
