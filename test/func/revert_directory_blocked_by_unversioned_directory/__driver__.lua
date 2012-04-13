mtn_setup()

-- reverting a renamed directory that has been replaced by an unversioned directory
-- under the original name should recreate the original directory and remove the
-- previously renamed copy

mkdir("foo")
check(mtn("add", "foo"), 0, false, false)

commit()

check(mtn("mv", "foo", "bar"), 0, false, false)

-- create an unversioned directory foo blocking the revert of the renamed foo
mkdir("foo")

check(mtn("revert", "."), 0, false, false)

xfail_if(exists("bar"))
