mtn_setup()

-- reverting a renamed directory that has been replaced by an unversioned file
-- should remove the unversioned file, recreate the original directory and
-- remove the renamed directory

mkdir("foo")
check(mtn("add", "foo"), 0, false, false)

commit()

check(mtn("mv", "foo", "bar"), 0, false, false)

-- create file blocking revert of foo
writefile("foo", "foo")

xfail(mtn("revert", "."), 0, false, false)

xfail_if(exists("bar"))
