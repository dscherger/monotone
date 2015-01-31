
mtn_setup()

mkdir("dir")
addfile("dir/file", "file")
commit()

remove("dir")
writefile("dir", "this isn't a directory")

check(mtn("status"), 0, true, false)
check(qgrep("not a directory:   dir", "stdout"))
check(mtn("diff"), 1, false, false)
