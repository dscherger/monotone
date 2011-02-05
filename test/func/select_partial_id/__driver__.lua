
mtn_setup()

addfile("testfile", "this is just a file")
commit()
rev = base_revision()

-- check that automate select returns the correct id when given a partial one
check(mtn("automate", "select", string.sub(rev,1,8)), 0, true, false)
check(qgrep(rev, "stdout"))

-- also check that invalid hex digits in partial ids lead to a proper error message
check(mtn("automate", "select", "p:abTcd"), 1, false, true)
check(qgrep("bad character 'T' in id name 'abTcd'", "stderr"))
