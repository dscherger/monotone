
mtn_setup()

check(get("testhooks.lua"))
writefile("dummy")

check(mtn("--rcfile=testhooks.lua", "add", "dummy"), 0, false, false)
check(exists("outfile"))
