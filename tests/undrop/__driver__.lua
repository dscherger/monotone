-- Test 'undrop' command
-- fixes bug#13604

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

-- drop undrop directory with a changed file
mkdir("dir1")
addfile("dir1/file1", "file1")
addfile("dir1/file2", "file2")
writefile("changed", "base")
commit()

writefile("dir1/file1", "file1-changed")
check(mtn("drop", "--recursive", "dir1"), 0, false, true)
check(qgrep("file dir1/file1 changed - it will be dropped but not deleted", "stderr"))
check(qgrep("directory dir1 not empty - it will be dropped but not deleted", "stderr"))
check(qgrep("dropping dir1/file2 from workspace", "stderr"))
check(qgrep("dropping dir1 from workspace", "stderr"))

check(mtn("undrop", "dir1"), 0, false, true)
check(readfile("dir1/file1")=="file1-changed")
check(readfile("dir1/file2")=="file2")
check(mtn("status"), 0, true, false)
check(qgrep("patched  dir1/file1", "stdout"))

-- drop undrop directory with no changed file
writefile("dir1/file1", "file1")

check(mtn("drop", "--recursive", "dir1"), 0, false, true)
check(qgrep("dropping dir1/file1 from workspace", "stderr"))
check(qgrep("dropping dir1/file2 from workspace", "stderr"))
check(qgrep("dropping dir1 from workspace", "stderr"))

check(mtn("undrop", "dir1"), 0, false, true)
check(readfile("dir1/file1")=="file1")
check(readfile("dir1/file2")=="file2")
check(mtn("status"), 0, true, false)
check(qgrep("no changes", "stdout"))

-- file that was not dropped. 'revert' doesn't report an error for
-- this, so 'undrop' doesn't either.
check(mtn("undrop", "unchanged"), 0, false, false)

-- end of file
