mtn_setup()

writefile("testfile", "foo blah")

check(mtn("add", "testfile"), 0, false, false)
commit()
base = base_revision()

writefile("testfile", "bar blah")
commit()
left = base_revision()

revert_to(base)

check(mtn("drop", "testfile"), 0, false, false)
commit()

check(mtn("merge", "--livelivelive"), 0, false, false)

check(mtn("update"), 0, false, false)

check(exists("testfile"))
