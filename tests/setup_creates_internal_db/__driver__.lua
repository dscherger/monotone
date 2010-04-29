
check(getstd("test_keys"))
check(getstd("test_hooks.lua"))
check(getstd("min_hooks.lua"))

check(nodb_mtn("setup", "--branch=testbranch", "."), 0, false, false)

check(exists("_MTN/mtn.db"))
check(fsize("_MTN/mtn.db") > 0)
