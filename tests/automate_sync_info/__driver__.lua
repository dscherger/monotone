
mtn_setup()

addfile("foo", "blah")
addfile(".mtn-sync-test", "one")
check(mtn("commit", "--date=2005-05-21T12:30:51", "--branch=testbranch",
          "--message=blah-blah"), 0, false, false)
base = base_revision()

addfile("foo2","blah2")
check(mtn("commit", "--branch=testbranch", "--message=two"), 0, false, false)
rev1 = base_revision()

writefile("foo2","blah3")
check(mtn("commit", "--branch=testbranch", "--message=three"), 0, false, false)
rev2 = base_revision()

--

check(mtn("automate", "find_newest_sync", "test"), 0, true, false)
check(readfile("stdout") == base)

check(mtn("automate", "find_newest_sync", "notest"), 1, true, false)
check(readfile("stdout") == "")

check(mtn("automate", "get_sync_info", base, "test"), 0, true, false)
check(readfile("stdout") == "one")

check(mtn("automate", "put_sync_info", rev1, "test", "one-two"), 0, true, false)
check(readfile("stdout") == "")

-- 

check(mtn("automate", "find_newest_sync", "test"), 0, true, false)
check(readfile("stdout") == rev1)

check(mtn("automate", "get_sync_info", rev1, "test"), 0, true, false)
check(readfile("stdout") == "one-two")

