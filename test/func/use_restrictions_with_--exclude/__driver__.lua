
mtn_setup()

mkdir("foo")
writefile("file1", "x")
writefile("foo/bar", "y")

check(mtn("add", "file1"), 0, false, false)
check(mtn("add", "foo/bar"), 0, false, false)

check(mtn("ci", "--exclude", ".", "-m", 'x'), 1, false, false)

-- this is dumb. excluding the whole tree and including file1 is
-- equivalent to just including file1. except that at this point 
-- the root dir has not been created and excluding the whole tree
-- excludes this creation. this causes the commit to fail because
-- file1 has no parent.

-- implicitly including all parents of explicitly included nodes
-- fixes this problem but will include changes to the parent nodes

-- explicit exclude and implicit include of "."

check(mtn("ci", "--exclude", ".", "file1", "-m", "x"), 0, false, false)
check(mtn("status"), 0, true, false)
check(qgrep("foo/bar", "stdout"))
check(not qgrep("file1", "stdout"))
append("file1", "a")

check(mtn("ci", "--exclude", "foo", "-m", 'x'), 0, false, false)
check(mtn("status"), 0, true)
check(qgrep("foo/bar", "stdout"))
check(not qgrep("file1", "stdout"))
append("file1", "a")

check(mtn("ci", ".", "--exclude", "file1", "-m", 'x'), 0, false, false)
check(mtn("status"), 0, true)
check(not qgrep("foo/bar", "stdout"))
check(qgrep("file1", "stdout"))
append("foo/bar", "b")

-- explicit exclude and implicit include of foo

check(mtn("ci", ".", "--exclude", "foo", "foo/bar", "-m", 'x'), 0, false, false)
check(mtn("status"), 0, true)
check(not qgrep("foo/bar", "stdout"))
check(not qgrep("file1", "stdout"))
append("file1", "a")
append("foo/bar", "b")

check(mtn("ci", "--exclude", "foo", "foo/bar", "-m", 'x'), 0, false, false)
check(mtn("status"), 0, true)
check(not qgrep("foo/bar", "stdout"))
check(qgrep("file1", "stdout"))
