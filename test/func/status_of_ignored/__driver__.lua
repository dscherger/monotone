-- Test for issue 193
--
-- Original problem:
--
-- mtn status foo.bak produces confusing output, assuming '*.bak' is
-- in the list of ignore file patterns (it is in the default list).
--
-- The problem report states this was noticed in mtn 1.0 (released
-- in 2011), but annotate says the relevant fix was added in 2008.

mtn_setup()

-- add a real file, to see what a 'normal' status report looks like
addfile("file_1", "file_1")
commit()

check(mtn("status", "file_1"), 0, true, nil)
check(qgrep("No changes", "stdout"))

-- Now gives an error:
check(mtn("status", "foo.bak"), 1, nil, true)
check(qgrep("warning: restriction includes unknown path 'foo.bak'", "stderr"))

-- Try again with an existing foo.bak
writefile("foo.bak", "foo.bak")
check(mtn("status", "foo.bak"), 1, nil, true)
check(qgrep("warning: restriction includes unknown path 'foo.bak'", "stderr"))
