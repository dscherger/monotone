
-- we can't create the test files which trigger the warnings on win anyways
skip_if(ostype=="Windows" or string.sub(ostype, 1, 6) == "CYGWIN")

mtn_setup()

mkdir("foo")
writefile("foo/\\", "invalid path")

check(mtn("add", "--recursive", "foo"), 0, false, true)
check(qgrep("skipping file 'foo/\\\\' with unsupported name", "stderr"))
check(qgrep("adding 'foo' to workspace manifest", "stderr"))

check(mtn("add", "foo/\\"), 1, false, true)
check(qgrep("misuse: path 'foo/\\\\' is invalid", "stderr"))

check(mtn("automate", "inventory"), 0, true, true)
check(qgrep("skipping file 'foo/\\\\' with unsupported name", "stderr"))
check(qgrep('path "foo"', "stdout"))
