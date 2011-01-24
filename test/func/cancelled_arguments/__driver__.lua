mtn_setup()

addfile("foo", "bar")
commit()
writefile("foo", "foo")
commit()

check(mtn("log"), 0, true, false)
rename("stdout", "nodiffs")
check(mtn("log", "--diffs"), 0, true, false)
rename("stdout", "diffs")
check(mtn("log", "--diffs", "--no-diffs"), 0, true, false)
check(samefile("nodiffs", "stdout"))
check(not samefile("diffs", "stdout"))