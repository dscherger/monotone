--
-- This is a test for bug #29927 - before monotone 0.47 if clone 
-- was executed in a directory with an existing bookkeeping directory
-- then this directory was removed on failure, actively destroying an
-- existing workspace
--


mtn_setup()
check(exists("_MTN"))

check(mtn("setup", "-b", "test.branch", "."), 1, false, true)
check(qgrep("bookkeeping directory already exists", "stderr"))
check(exists("_MTN"))

check(mtn("clone", "somewhere", "test.branch", "."), 1, false, true)
check(qgrep("bookkeeping directory already exists", "stderr"))
check(exists("_MTN"))
