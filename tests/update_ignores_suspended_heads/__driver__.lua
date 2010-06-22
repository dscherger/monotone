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
