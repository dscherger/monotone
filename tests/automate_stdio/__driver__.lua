include("/common/automate_stdio.lua")

mtn_setup()

-- a number of broken input strings
run_stdio("le", 1)
run_stdio("l", 1)
run_stdio("l5:a", 1)
run_stdio("l5:aaaaaaaa", 1)
run_stdio("x6:leavese", 1)
run_stdio("o3:key0:ex6:leavese", 1)
run_stdio("o3:ke0:el6:leavese", 1)
-- unknown command
run_stdio("l9:foobarbaze", 1)
-- multiple expansions ('cert' and 'certs')
run_stdio("l3:cere", 1)
-- invalid ('leaves' doesn't take --author)
run_stdio("o6:author3:fooe l6:leavese", 1)

-- misuse: 'get_revision' needs an argument
run_stdio("l12:get_revisione", 2)

-- not broken
run_stdio("o3:key0:el6:leavese", 0)
run_stdio("o3:key0:e l6:leavese", 0)

-- ensure that we get the output we expect
writefile("output", "file contents")
check(mtn("automate", "inventory", "--no-unknown"), 0, true, false)
canonicalize("stdout")
rename("stdout", "stdio-inventory")
check(run_stdio("o10:no-unknown0:e l9:inventorye", 0) == readfile("stdio-inventory"))

