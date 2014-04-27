mtn_setup()

check(mtn(''), 2, false, true)
check(qgrep("Usage: ", "stderr"))

check(mtn('ls', ''), 1, false, true)
check(qgrep("is ambiguous", "stderr"))