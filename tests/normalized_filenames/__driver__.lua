
mtn_setup()

writefile("foo", "blah blah")
-- The UI used to fix these, while later code did not, so let's check
-- the inner code directly.
append("_MTN/workrev", '\nadd_dir "."\n')
check(mtn("automate", "get_manifest_of"), 3, false, false)

append("_MTN/workrev", '\nadd_dir "./bar"\n')

check(mtn("automate", "get_manifest_of"), 3, false, false)
check(mtn("automate", "get_revision"), 3, false, false)
check(mtn("commit", "--message=foo", "--branch=foo"), 3, false, false)
