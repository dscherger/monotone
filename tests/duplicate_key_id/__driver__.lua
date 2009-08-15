mtn_setup()

remove("test.db")
check(mtn("db", "init"), 0, false, false)

check(get("bad_test_key", "stdin"))
check(mtn("read"), 0, false, false, true)

addfile("testfile", "version 0 of test file")
check(mtn("commit", "-m", "try to commit with bad key in DB"), 0, false, true)

check(mtn("ls", "keys"), 0, false, true)
check(qgrep("Duplicate Key: tester@test.net", "stderr"))

check(get("local_name.lua"))
check(mtn("ls", "keys", "--rcfile", "local_name.lua"), 0, false, true)
check(not qgrep("Duplicate Key:", "stderr"))
