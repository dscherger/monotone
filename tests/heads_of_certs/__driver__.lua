mtn_setup()

addfile("testfile", "this is just a file\n")
commit("testbranch")
first = base_revision()

writefile("testfile", "Now, this is a different file\n")
commit("testbranch")
second = base_revision()

writefile("testfile", "And we change it a third time\n")
commit("testbranch")
third = base_revision()

check(mtn("cert", first, "testcert", "value=with=equal=signs"))
check(mtn("cert", second, "testcert", "value"))

-- Check that a log without H: gives both the first and second commit...
check(mtn("log", "--from=c:testcert"), 0, true, true)
check(qgrep("expanded to '"..first, "stderr"))
check(qgrep("expanded to '"..second, "stderr"))

-- Check that a log with H: gives only the second commit...
check(mtn("log", "--from=H:c:testcert"), 0, true, true)
check(not qgrep("expanded to '"..first, "stderr"))
check(qgrep("expanded to '"..second, "stderr"))
-- Note that if the third revision is in the log, something else is wrong...
-- There is really no case for this last test, just paranoia.
check(not qgrep(third, "stderr"))
