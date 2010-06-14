
mtn_setup()

-- The 'version' command behaves exactly as the '--version' option.
check(mtn("version"), 0, true, 0)
rename("stdout", "out")
check(mtn("--version"), 0, true, 0)
check(samefile("stdout", "out"))

-- The 'version' command prints no detailed information.
check(mtn("version"), 0, true, 0)
output = readfile("stdout")
check(string.find(output, "Running on") == nil)

-- The 'version' command allows a '--verbose' option.
check(mtn("version", "--verbose"), 0, true, 0)
output = readfile("stdout")
check(string.find(output, "Running on") ~= nil)

-- The '--version' option does not allow a '--verbose' option (because the
-- latter is not global).
check(mtn("--version", "--verbose"), 0, true, 0)
output = readfile("stdout")
check(string.find(output, "Running on") == nil)
