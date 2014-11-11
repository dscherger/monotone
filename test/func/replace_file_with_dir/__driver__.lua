
mtn_setup()

addfile("file", "file")
commit()

remove("file")
mkdir("file")
check(mtn("status"), 0, true, false)
check(qgrep("not a file:        file", "stdout"))
check(mtn("diff"), 1, false, false)
