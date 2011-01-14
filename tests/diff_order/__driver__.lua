mtn_setup()

check(get("expected1"))
check(get("expected2"))

-- add two files in two revisions, where the second
-- gets a bigger internal node id than the first
addfile("ccc", "foobar")
commit()
addfile("bbb", "barbaz")
commit()

writefile("bbb", "new stuff")
writefile("ccc", "new stuff")

-- now ensure that the patch is not ordered by node id,
-- where bbb would have to come first, but by file name
check(mtn("diff"), 0, true, false)
check(samefile("stdout", "expected1"))

commit()

-- add a new file and drop an existing one, the order
-- should still be alphabetical
addfile("aaa", "even newer")
writefile("ccc", "even newer")
check(mtn("drop", "bbb"), 0, false, false)

check(mtn("diff"), 0, true, false)
check(samefile("stdout", "expected2"))

