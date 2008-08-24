
mtn_setup()

writefile("original", "some stuff here")

check(mtn("add", "original"), 0, false, false)
commit()
base = base_revision()

-- drop it
check(mtn("drop", "--bookkeep-only", "original"), 0, false, false)
commit()

revert_to(base)

-- patch and rename it
rename("original", "different")
check(mtn("rename", "--bookkeep-only", "original", "different"), 0, false, false)
append("different", "more\n")
commit()

check(mtn("merge"), 1, nil, true)
check(qgrep("conflict: file 'different' dropped on the left, changed on the right", "stderr"))

-- In mtn 0.40 and earlier, the merge succeeded, now it doesn't

