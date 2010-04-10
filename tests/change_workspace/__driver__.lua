-- Test Lua extension 'change_workspace'

mtn_setup()

-- We can't run Lua extensions directly from here, so we override the
-- normal test_hooks to provide a function that calls change_workspace
check(get("test_hooks.lua"))

mkdir("aaron")
mkdir("betty")
check(indir("aaron", mtn("setup", "--branch=aaronbranch", ".")), 0, false, false)
check(indir("betty", mtn("setup", "--branch=bettybranch", ".")), 0, false, false)

check(mtn("get_branch", "aaron"), 0, true, true)
check(readfile("stdout") == "aaronbranch\n")

check(mtn("get_branch", "betty"), 0, true, true)
check(readfile("stdout") == "bettybranch\n")

-- error cases
check(mtn("get_branch", "bogus"), 0, true, true)
check(qgrep("error: cannot change to directory .*/bogus", "stdout"))

mkdir("other")
check(mtn("get_branch", "other"), 0, true, true)
check(qgrep("directory .*/other is not a workspace", "stdout"))

-- end of file
