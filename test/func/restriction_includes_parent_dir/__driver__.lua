
mtn_setup()

-- Restricting a new project to a single file excludes the root directory
-- addition. The new implicit include semantics cause the root directory to
-- be included as the required parent of the specified file.

addfile("file", "file")

-- exclude newly added root dir but include file below it
-- this will implicitly include the root dir

check(mtn("st", "file"), 0, true, true)
rename("stdout", "additions")

check(grep("added", "additions"), 0, true, false)
check(numlines("stdout") == 2)

commit()

mkdir("foo")

addfile("foo/bar", "foobar")

-- exclude newly added foo dir but include bar below it
-- this will implicitly include the new foo dir and the root dir

check(mtn("st", "foo/bar"), 0, true, true)
rename("stdout", "additions")

check(grep("added", "additions"), 0, true, false)
check(numlines("stdout") == 2)

-- ensure that --depth=0 means "directory without contents" rather
-- than "directory and all immediate children"

check(mtn("st", "--depth", "0"), 0, true, false)
check(qgrep("no changes", "stdout"))

check(mtn("st", "--depth", "1"), 0, true, false)
check(qgrep("foo", "stdout"))
check(not qgrep("foo/bar", "stdout"))

check(mtn("st", "--depth", "2"), 0, true, false)
check(qgrep("foo", "stdout"))
check(qgrep("foo/bar", "stdout"))

-- ensure that --depth is relative to the current directory

chdir("foo")
check(mtn("st", ".", "--depth", "0"), 0, true, false)
check(qgrep("foo", "stdout"))
check(not qgrep("foo/bar", "stdout"))

check(mtn("st", ".", "--depth", "1"), 0, true, false)
check(qgrep("foo", "stdout"))
check(qgrep("foo/bar", "stdout"))

-- ensure that --depth is relative to the specified directory

chdir("..")
check(mtn("st", "foo", "--depth", "0"), 0, true, false)
check(qgrep("foo", "stdout"))
check(not qgrep("foo/bar", "stdout"))

check(mtn("st", "foo", "--depth", "1"), 0, true, false)
check(qgrep("foo", "stdout"))
check(qgrep("foo/bar", "stdout"))
