mtn_setup()

-- reverting a directory that has been replaced by a file should remove the
-- file and recreate the directory

mkdir("foo")
check(mtn("add", "foo"), 0, false, false)

commit()

-- replace foo with a file
remove("foo")
writefile("foo", "foo")

xfail(mtn("revert", "."), 0, false, false)
