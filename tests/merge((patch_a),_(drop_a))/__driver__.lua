
mtn_setup()

writefile("base", "foo blah")
writefile("left", "bar blah")

copy("base", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()
base = base_revision()

copy("left", "testfile")
commit()
left = base_revision()

revert_to(base)

check(mtn("drop", "testfile"), 0, false, false)
commit()

check(mtn("merge"), 1, nil, true)

check(qgrep("conflict: file 'testfile' dropped on the left, changed on the right", "stderr"))

