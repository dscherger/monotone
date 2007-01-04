
mtn_setup()

check(get("testhooks.lua"))

writefile("dummy")
check(mtn("--rcfile=testhooks.lua", "--debug", "add", "dummy"), 0, false, false)
skip_if(exists("skipfile"))
check(exists("outfile"))
