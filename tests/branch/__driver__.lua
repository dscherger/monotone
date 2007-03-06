mtn_setup()

addfile("foo", "blabla")
-- ensure that commit no longer accepts -b
check(mtn("commit", "-bfoo"), 1, false, false)
-- do the commit for real now
commit()

-- check parameter checking
check(mtn("branch"), 2, false, false)
check(mtn("branch", "foo", "bar"), 2, false, false)

-- check if the workspace is already set to the existing branch
check(mtn("branch", "testbranch"), 1, false, true)
check(qgrep("workspace is already set to testbranch", "stderr"))

-- check for the correct wording (new vs existing)
check(mtn("branch", "new_branch"), 0, false, true)
check(qgrep("the new branch new_branch", "stderr"))

check(mtn("branch", "testbranch"), 0, false, true)
check(qgrep("the existing branch testbranch", "stderr"))

-- TODO: check for the divergence warning

