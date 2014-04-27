
mtn_setup()
addfile("file", "file")
adddir("dir")
adddir("dir/subdir")
commit()

check(mtn("mv", "file", "file"), 1, false, true)
check(qgrep("destination 'file/' is not a directory", "stderr"))

check(mtn("mv", "dir", "dir"), 0, false, true)
check(qgrep("cannot move 'dir' to a subdirectory of itself, 'dir/dir'", "stderr"))

check(mtn("mv", "dir", "dir/subdir"), 0, false, true)
check(qgrep("cannot move 'dir' to a subdirectory of itself, 'dir/subdir/dir'", "stderr"))
