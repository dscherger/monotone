
-- we can't create the test files which trigger the warnings on win anyways
skip_if(ostype=="Windows")

mtn_setup()

mkdir("foo")
writefile("foo/\\", "invalid path")

check(mtn("add", "foo"), 0, false, true)
check(qgrep("skipping invalid path '\\\\'", "stderr"))
check(qgrep("adding foo to workspace manifest", "stderr"))

check(mtn("automate", "inventory"), 0, true, true)
check(qgrep("skipping invalid path '\\\\'", "stderr"))
check(qgrep('path "foo"', "stdout"))

