
mtn_setup()

check(mtn("pull", "nosuchhost__blahblah__asdvasoih.com", "some.pattern"), 1, false, true)
check(qgrep("name resolution failure for nosuchhost__blahblah__asdvasoih.com", "stderr"))

check(mtn("pull", "mtn:localhost?*"), 1, false, true)
check(qgrep("a non-empty hostname is expected for the 'mtn' uri scheme", "stderr"))
