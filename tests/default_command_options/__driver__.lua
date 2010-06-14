-- Demonstrate that get_default_command_options works, and that
-- command line options override it. Also show that --verbose
-- --no-verbose works.

mtn_setup()

check(get("default_options.lua"))

addfile("foo", "bar")
commit()

check(mtn("log"), 0, true, false)
rename("stdout", "normallog")

check(mtn("log", "--brief"), 0, true, false)
rename("stdout", "brieflog")

check(mtn("log", "--rcfile=default_options.lua"), 0, true, false)
check(samefile("stdout", "brieflog"))

check(mtn("log", "--brief", "--no-brief"), 0, true, false)
check(samefile("stdout", "normallog"))

check(mtn("log", "--rcfile=default_options.lua", "--no-brief"), 0, true, false)
check(samefile("stdout", "normallog"))

check(mtn("version", "--no-verbose"), 0, true, false)
rename("stdout", "normalversion")

check(mtn("version", "--verbose"), 0, true, false)
rename("stdout", "fullversion")

check(mtn("version", "--rcfile=default_options.lua"), 0, true, false)
check(samefile("stdout", "fullversion"))

check(mtn("version", "--verbose", "--no-verbose"), 0, true, false)
check(samefile("stdout", "normalversion"))

check(mtn("version", "--rcfile=default_options.lua", "--no-verbose"), 0, true, false)
check(samefile("stdout", "normalversion"))

check(mtn("status", "--rcfile=default_options.lua"), 1, false, false)

