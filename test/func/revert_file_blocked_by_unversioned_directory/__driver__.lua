mtn_setup()

-- reverting a renamed file that has been replaced by an unversioned directory
-- should remove the unversioned directory, recreate the original file and
-- remove the renamed file

addfile("foo", "foo")

commit()

check(mtn("mv", "foo", "bar"), 0, false, false)

-- create directory blocking revert of foo
mkdir("foo")

xfail(mtn("revert", "."), 0, false, false)

xfail_if(exists("bar"))
