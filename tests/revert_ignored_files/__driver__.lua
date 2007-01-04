
mtn_setup()

-- revert, with only ignored files listed on the command line, should not
-- revert anything

addfile("foo", "foo")
addfile("bar", "bar")
addfile("baz", "baz")

writefile("foo.ignored", "foo.ignored")
writefile("bar.ignored", "bar.ignored")
writefile("baz.ignored", "baz.ignored")

commit()

writefile("foo", "foofoo")
writefile("bar", "barbar")
writefile("baz", "bazbaz")

append(".mtn-ignore", "\\.ignored$\n")

check(mtn("status"), 0, true, false)
check(qgrep("foo", "stdout"))
check(qgrep("bar", "stdout"))
check(qgrep("baz", "stdout"))

check(mtn("status", "foo.ignored", "bar.ignored", "baz.ignored"), 0, true, false)
check(not qgrep("foo", "stdout"))
check(not qgrep("bar", "stdout"))
check(not qgrep("baz", "stdout"))

-- revert with nothing but ignored files should do nothing

check(mtn("revert", "foo.ignored", "bar.ignored", "baz.ignored"))

check(mtn("status"), 0, true, false)
check(qgrep("foo", "stdout"))
check(qgrep("bar", "stdout"))
check(qgrep("baz", "stdout"))
