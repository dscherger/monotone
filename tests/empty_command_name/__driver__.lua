mtn_setup()

check(mtn(''), 1, false, true)
check(qgrep("is ambiguous", "stderr"))

check(mtn('ls', ''), 1, false, true)
check(qgrep("is ambiguous", "stderr"))
