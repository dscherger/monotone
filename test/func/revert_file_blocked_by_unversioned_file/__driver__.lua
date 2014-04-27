mtn_setup()

-- reverting a renamed file that has been replaced by an unversioned file
-- under the original name should recreate the original file and remove the
-- previously renamed copy

addfile("foo", "foo")

commit()

check(mtn("mv", "foo", "bar"), 0, false, false)

-- create an unversioned file foo blocking the revert of the renamed foo
writefile("foo", "unversioned")

check(mtn("revert", "."), 0, false, false)

xfail_if(exists("bar"))
