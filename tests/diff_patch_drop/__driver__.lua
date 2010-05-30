skip_if(not existsonpath("patch"))

mtn_setup()

mkdir("dir")
addfile("dir/file", "foobar")
id = sha1("dir/file")

commit()

check(mtn("drop", "dir/file"), 0, false, false)
check(mtn("diff"), 0, true, false)
rename("stdout", "diff")

-- the target of a deletion should be /dev/null

check(qgrep("^--- dir/file	" .. id .. "$", "diff"))
check(qgrep("^\\+\\+\\+ /dev/null	$", "diff"))

check(mtn("revert", "dir/file"), 0, false, false)
check(exists("dir/file"))

-- patch should remove the file
-- but it is a little aggressive and removes the dir too!

copy("diff", "stdin")
check({"patch", "-p0"}, 0, false, false, true)

check(not exists("dir/file"))
check(not exists("dir"))
