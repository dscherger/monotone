
mtn_setup()

check(get("commit_validate.lua"))
check(get("errmsg"))

addfile("test", "this is a\012\013test")
check(mtn("--rcfile=commit_validate.lua", "commit", "-m", "foo"), 1, false, true)
canonicalize("stderr")
check(samefile("errmsg", "stderr"))

writefile("test", "this is a\ntest")
check(mtn("--rcfile=commit_validate.lua", "commit", "-m", "foo"), 0, false, false)

