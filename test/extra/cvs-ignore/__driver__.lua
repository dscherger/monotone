mtn_setup()

-- Prepare the test by copying the lua hook to test
-- and adding a few lines to test_hooks.lua
mkdir("hooks.d")
check(copy(srcdir.."/../extra/mtn-hooks/monotone-cvs-ignore.lua",
	   "hooks.d/monotone-cvs-ignore.lua"))

append("test_hooks.lua", "\
\
includedirpattern(get_confdir() .. \"/hooks.d\",\"*.conf\")\
includedirpattern(get_confdir() .. \"/hooks.d\",\"*.lua\")\
")

-- Do the test
writefile("test1.txt", "foo")
mkdir("subdir")
writefile("subdir/test2.txt", "foo")

check(mtn("ls","unknown"), 0, true, false)
check(grep("^test1\\.txt$", "stdout"), 0, false, false)

writefile("subdir/.cvsignore", "*.txt")

check(mtn("ls","unknown"), 0, true, false)
check(grep("^test1\\.txt$", "stdout"), 0, false, false)
check(grep("^subdir/test2\\.txt$", "stdout"), 1, false, false)

writefile(".cvsignore", "*.txt")

check(mtn("ls","unknown"), 0, true, false)
check(grep("^test1\\.txt$", "stdout"), 1, false, false)
check(grep("^subdir/test2\\.txt$", "stdout"), 1, false, false)
