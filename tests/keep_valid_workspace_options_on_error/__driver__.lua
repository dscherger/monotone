
mtn_setup()

addfile("foo", "bar")
commit()

check(mtn_ws_opts("status"), 0, false, false)

check(mtn("db", "init", "-d", "bar.mtn"), 0, false, false)
check(mtn_ws_opts("status"), 0, false, false)

check(mtn_ws_opts("status", "-d", "bar.mtn"), 1, false, true)
check(qgrep("did you specify the wrong database?", "stderr"))

check(mtn_ws_opts("status"), 0, false, false)


addfile("bar", "foo")
check(mtn_ws_opts("commit", "-m", "foo", "-b", "new-branch"), 0, false, false)

check(qgrep("new-branch", "_MTN/options"))

