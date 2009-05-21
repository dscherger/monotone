mtn_setup()

-- reverting a renamed directory that has been replaced by a file should
-- recreate the directory and remove the file

mkdir("foo")
check(mtn("add", "foo"), 0, false, false)

commit()

check(mtn("mv", "foo", "bar"), 0, false, false)

-- replace bar with a file
remove("bar")
writefile("bar", "bar")       

check(mtn("revert", "."), 0, false, false)

xfail_if(exists("bar"))
