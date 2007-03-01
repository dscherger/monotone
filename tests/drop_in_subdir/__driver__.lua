
mtn_setup()

addfile("testdir1/file1", "file 1")
addfile("testdir2/file2", "file 2")

commit()

check(indir("testdir1", mtn("rm", "file1")), 0, false, false)
check(not exists("testdir1/file1"))

check(mtn("rm", "testdir2/file2"), 0, false, false)
check(exists("testdir2"))
check(not exists("testdir2/file2"))

