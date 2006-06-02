
mtn_setup()

mkdir("testdir")
addfile("testdir/foo", "blah blah blah")
commit()
base = base_revision()

remove_recursive("testdir")
check(cmd(mtn("drop", "testdir/foo", "testdir")), 0, false, false)
commit()

revert_to(base)

check(exists("testdir"))
check(cmd(mtn("update")), 0, false, false)

check(base ~= base_revision())

check(not exists("testdir"))
