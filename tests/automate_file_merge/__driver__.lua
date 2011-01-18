-- Test automate file_merge

mtn_setup()

addfile("foo", "foo")
addfile("bar", "bar\none\ntwo\nthree\n")
addfile("baz", "baz\r\naaa\r\nbbb\r\nccc")
commit("testbranch", "base")
base = base_revision()

writefile("foo", "foo\nfirst\nrevision")
writefile("bar", "bar\nzero\none\ntwo\nthree\n")
writefile("baz", "baz\r\nAAA\r\nbbb\r\nccc")
commit("testbranch", "first")
first = base_revision()

revert_to(base)

writefile("foo", "foo\nsecond\nrevision")
writefile("bar", "bar\none\ntwo\nthree\nfour\n")
writefile("baz", "baz\r\naaa\r\nbbb\r\nCCC")
commit("testbranch", "second")
second = base_revision()

check(mtn("automate", "file_merge", first, "foo", second, "foo"), 1, nil, true)
check(qgrep("internal line merger failed", "stderr"))

writefile("expected_bar", "bar\nzero\none\ntwo\nthree\nfour\n")
check(mtn("automate", "file_merge", first, "bar", second, "bar"), 0, true, nil)
canonicalize("stdout")
check(samefile("expected_bar", "stdout"))

writefile("expected_baz", "baz\r\nAAA\r\nbbb\r\nCCC")
check(mtn("automate", "file_merge", first, "baz", second, "baz"), 0, true, nil)
--canonicalize("stdout")
check(samefile("expected_baz", "stdout"))

-- end of file
