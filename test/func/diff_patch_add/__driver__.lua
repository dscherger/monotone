skip_if(not existsonpath("patch"))

mtn_setup()

mkdir("dir")
addfile("dir/file", "foobar")
id = sha1("dir/file")

check(mtn("diff"), 0, true, false)
rename("stdout", "diff")

-- the source of an addition should be /dev/null

check(qgrep("^--- /dev/null	$", "diff"))
check(qgrep("^\\+\\+\\+ dir/file	" .. id .. "$", "diff"))

-- patch should re-create the file

remove("dir/file")
copy("diff", "stdin")
check({"patch", "-p0"}, 0, false, false, true)

check(exists("dir/file"))
check(qgrep("foobar", "dir/file"))
