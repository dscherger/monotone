-- Test 'undrop' command

mtn_setup()

addfile("changed", "base")
commit()

-- With no changes before 'drop', 'undrop' is just 'revert'
check(mtn("drop", "changed"), 0, false, false)
check(mtn("undrop", "changed"), 0, false, false)
check(readfile("changed")=="base")

check(mtn("status"), 0, true, false)
check(qgrep("no changes", "stdout"))

-- With changes before 'drop', 'undrop' is like 'revert --bookkeeponly'
writefile("changed", "modified")
check(mtn("drop", "changed"), 0, false, true)
check(qgrep("file changed changed - it will be dropped but not deleted", "stderr"))
check(mtn("undrop", "changed"), 0, false, false)
check(readfile("changed")=="modified")
check(mtn("status"), 0, true, false)
check(qgrep("patched  changed", "stdout"))

-- one changed, one unchanged file
addfile("unchanged", "base")
writefile("changed", "base")
commit()

writefile("changed", "modified")
check(mtn("drop", "changed", "unchanged"), 0, false, true)
check(qgrep("file changed changed - it will be dropped but not deleted", "stderr"))
check(qgrep("dropping unchanged from workspace", "stderr"))
check(mtn("undrop", "changed", "unchanged"), 0, false, false)
check(readfile("changed")=="modified")
check(readfile("unchanged")=="base")
check(mtn("status"), 0, true, false)
check(qgrep("patched  changed", "stdout"))
check(not qgrep("patched  unchanged", "stdout"))

-- FIXME: test recursive, errors

-- end of file
