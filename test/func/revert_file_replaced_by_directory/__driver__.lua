mtn_setup()

-- reverting a file that has been replaced by a directory should remove the
-- directory and recreate the file

addfile("foo", "foo")

commit()

-- replace foo with a directory
remove("foo")
mkdir("foo")

xfail(mtn("revert", "."), 0, false, false)
