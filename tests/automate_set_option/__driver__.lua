
mtn_setup()

check({"cp", "test.db", "test2.db"}, 0, false, false)

--no args
check(mtn("automate", "set_option"), 1, false, false)

--one arg
check(mtn("automate", "set_option", "database"), 1, false, false)

--proper args, change database
check(mtn("automate", "set_option", "database", "test2.db"), 0, true, false)
check(qgrep("test2.db", "stdout"), 0, false, false)

--proper args, invalid database
check(mtn("automate", "set_option", "database", "test3.db"), 1, false, true)
check(qgrep("invalid db file", "stderr"), 0, false, false)

--proper args, set branch
check(mtn("automate", "set_option", "branch", "automate.set_option"), 0, true, false)
check(qgrep("automate.set_option", "stdout"))
check(qgrep("automate.set_option", "_MTN/options"))

check({"mkdir", "option_keydir"}, 0, false, false)

--invalid keydir
check(mtn("automate", "set_option", "keydir", "keydir_not_here"), 1, false, true)
check(qgrep("keydir does not exist", "stderr"))

--valid keydir
check(mtn("automate", "set_option", "keydir", "option_keydir"), 0, true, false)
check(qgrep("option_keydir", "stdout"))
check(qgrep("option_keydir", "_MTN/options"))

--invalid key (this key shouldn't be in 'keydir', which is currently set.
check(mtn("automate", "set_option", "key", "testkey@mtn.net"), 1, false, true)
check(qgrep("invalid key;", "stderr"))

--switch back to original keydir
check(mtn("automate", "set_option", "keydir", "keys"), 0, false, false)
--good key...
check(mtn("automate", "set_option", "key", "tester@test.net"), 0, true, false)
check(qgrep("tester@test.net", "stdout"))
check(qgrep("tester@test.net", "_MTN/options"))
