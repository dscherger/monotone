skip_if(not existsonpath("patch"))

mtn_setup()

mkdir("dir1")
mkdir("dir2")
addfile("dir1/file1", "foobar")
id1 = sha1("dir1/file1")

commit()

writefile("dir1/file1", "barfoo")
check(mtn("mv", "dir1/file1", "dir2/file2"), 0, false, false)
id2 = sha1("dir2/file2")

check(mtn("diff"), 0, true, false)
rename("stdout", "diff")

-- the source and targets of the diff shoud show a rename

check(qgrep("^--- dir1/file1	" .. id1 .. "$", "diff"))
check(qgrep("^\\+\\+\\+ dir2/file2	" .. id2 .. "$", "diff"))

check(mtn("revert", "."), 0, false, false)

-- revert will leave this laying around
check(exists("dir2"))
remove("dir2")
check(exists("dir1/file1"))

-- ideally patch would actuall rename the file and directory but alas it doesn't
-- it does still seem to apply the content changes

copy("diff", "stdin")
check({"patch", "-p0"}, 0, false, false, true)

check(exists("dir1/file1"))
check(not exists("dir2/file2"))
check(qgrep("barfoo", "dir1/file1"))
