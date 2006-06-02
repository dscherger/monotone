
mtn_setup()

writefile("foo", "some data")
-- Check a single character filename too, because those have had bugs.
writefile("a", "some data")

check(cmd(mtn("add", "foo")), 0, false, false)
check(cmd(mtn("add", "a")), 0, false, false)
check(cmd(mtn("attr", "set", "foo", "test:test_attr", "true")), 0, false, false)
check(cmd(mtn("attr", "set", "a", "test:test_attr", "1")), 0, false, false)
commit()
co_r_sha1 = base_revision()

check(cmd(mtn("attr", "drop", "foo", "test:test_attr")), 0, false, false)
check(cmd(mtn("attr", "set", "a", "test:test_attr", "2")), 0, false, false)
commit()
update_r_sha1 = base_revision()

-- Check checkouts.
remove_recursive("co-dir")
check(cmd(mtn("checkout", "--revision", co_r_sha1, "co-dir")), 0, true)
check(qgrep("test:test_attr:foo:true", "stdout"))
check(qgrep("test:test_attr:a:1", "stdout"))

-- Check updates.
remove_recursive("co-dir")
check(cmd(mtn("checkout", "--revision", update_r_sha1, "co-dir")), 0, true)
check(not qgrep("test:test_attr:foo", "stdout"))
check(qgrep("test:test_attr:a:2", "stdout"))

-- check that files must exist to have attributes set
check(cmd(mtn("attr", "set", "missing", "mtn:execute")), 1, false, false)
