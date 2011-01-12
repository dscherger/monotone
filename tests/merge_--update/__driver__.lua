mtn_setup()

addfile("foo", "foo")
commit()
base = base_revision()

addfile("left", "left")
commit()
left = base_revision()
check(mtn("up", "-r", base), 0, nil, false)

addfile("right", "right")
commit()
right = base_revision()

check(mtn("merge"), 0, nil, true)
check(not qgrep("update", "stderr"))
check(mtn("heads"), 0, true, false)
check(not qgrep(base_revision(), "stdout"))

addfile("third", "third")
commit()
third = base_revision()

check(mtn("merge", "--update"), 0, nil, true)
check(qgrep("updated to", "stderr"))
check(mtn("heads"), 0, true, false)
check(qgrep(base_revision(), "stdout"))
