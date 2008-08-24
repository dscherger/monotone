
mtn_setup()

writefile("base", "foo blah")
writefile("left", "bar blah")
writefile("new_right", "baz blah")

copy("base", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()
base = base_revision()

copy("left", "testfile")
commit()

revert_to(base)

remove("testfile")
check(mtn("drop", "--bookkeep-only", "testfile"), 0, false, false)
commit()

copy("new_right", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()

check(mtn("merge"), 1, nil, true)
check(qgrep("conflict: duplicate name 'testfile' for the directory ''", "stderr"))
check(qgrep("conflict: file 'testfile' dropped on the right, changed on the left", "stderr"))

