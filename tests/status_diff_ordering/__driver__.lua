mtn_setup()

-- status and diff should list drops, then adds then patches.
-- files within each of these sections should be ordered by path.

-- this test relies on the fact that files get sequentially allocated node ids
-- based on their path order when they are initially committed

addfile("111", "111\n") -- hhh
addfile("222", "222\n") -- bbb
addfile("333", "333\n") -- ggg
addfile("444", "444\n") -- aaa
addfile("555", "555\n") -- iii
addfile("666", "666\n") -- ccc
commit()

-- rename so path order no longer matches the original node order

check(mtn("rename", "111", "hhh"), 0, false, false)
check(mtn("rename", "222", "bbb"), 0, false, false)
check(mtn("rename", "333", "ggg"), 0, false, false)
check(mtn("rename", "444", "aaa"), 0, false, false)
check(mtn("rename", "555", "iii"), 0, false, false)
check(mtn("rename", "666", "ccc"), 0, false, false)
commit()

-- drops

check(mtn("drop", "aaa", "bbb", "ccc"), 0, false, false)

-- patches

writefile("hhh", "hhh\n")
writefile("ggg", "ggg\n")
writefile("iii", "iii\n")

-- adds

addfile("eee", "eee\n")
addfile("fff", "fff\n")
addfile("ddd", "ddd\n")

check(get("status.output"))
check(get("diff.output"))

check(mtn("status"), 0, true, false)
actual = string.gsub(readfile("stdout"), "\nDate: [^\n]*", "")
expected = string.gsub(readfile("status.output"), "\nDate: [^\n]*", "")
check(expected == actual)

check(mtn("diff"), 0, true, false)
check(samefile("diff.output", "stdout"))
