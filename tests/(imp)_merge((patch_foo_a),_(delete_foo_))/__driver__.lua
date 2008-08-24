
mtn_setup()

mkdir("foo")
addfile("foo/a", "blah blah")
commit()
base = base_revision()

check(mtn("drop", "--bookkeep-only", "--recursive", "foo"), 0, false, false)
commit()

remove("foo")
revert_to(base)

writefile("foo/a", "some other stuff")
commit()

check(mtn("--branch=testbranch", "merge"), 1, nil, true)
check(qgrep("conflict: orphaned file 'foo/a' from revision", "stderr"))
check(qgrep("conflict: file 'foo/a' dropped on the right, changed on the left", "stderr"))

-- In mtn 0.40 and earlier, the merge succeeded, now it doesn't
