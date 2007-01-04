
mtn_setup()

mkdir("baz")
writefile("foo")
writefile("bar")
writefile("baz/xyzzy")

writefile(".mtn-ignore", "bar\nbaz\n*.d\n")

check(mtn("ls", "unknown"), 0, true, true)
copy("stdout", "unknown")
copy("stderr", "unknownerr")

check(qgrep("foo", "unknown"))
check(not qgrep("bar", "unknown"))
check(not qgrep("baz", "unknown"))
check(qgrep("warning", "unknownerr"))


check(mtn("ls", "ignored"), 0, true, true)
copy("stdout", "ignored")
copy("stderr", "ignorederr")

check(not qgrep("foo", "ignored"))
check(qgrep("bar", "ignored"))
check(qgrep("xyzzy", "ignored"))
check(qgrep("warning", "ignorederr"))
check(grep("-qv", "warning|skipping", "ignorederr"), 1)

remove(".mtn-ignore")
check(mtn("ls", "ignored"), 0, true, true)
check(qgrep("test.db", "stdout"))
check(not qgrep("warning", "stderr"))
check(grep("-qv", "warning|skipping", "ignorederr"), 1)
