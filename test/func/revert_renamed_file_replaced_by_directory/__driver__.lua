mtn_setup()

-- reverting a renamed file that has been replaced by a directory should
-- recreate the file and remove the directory

addfile("foo", "foo")

commit()

check(mtn("mv", "foo", "bar"), 0, false, false)

-- replace bar with a directory
remove("bar")
mkdir("bar")

check(mtn("revert", "."), 0, false, false)

xfail_if(exists("bar"))
