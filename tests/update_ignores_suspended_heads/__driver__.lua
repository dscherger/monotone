-- this is an accompanying test for monotone bug #29843
-- right now it succeeds, so we still have to find the exact
-- cause what went wrong in #29843

mtn_setup()

addfile("foofile", "foo")
commit()
base = base_revision()

addfile("badfile", "bad")
commit()
bad = base_revision()

check(mtn("update", "-r", base), 0, false, false)
addfile("goodfile", "good")
commit()
good = base_revision()

check(mtn("suspend", bad), 0, false, false)

check(mtn("update", "-r", base), 0, false, false)

check(mtn("update"), 0, false, false)
check(base_revision() == good)
