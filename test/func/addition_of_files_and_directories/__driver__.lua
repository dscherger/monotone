
mtn_setup()

mkdir("dir")
writefile("file0", "file 0\n")
writefile("dir/file1", "file 1\n")
writefile("dir/file2", "file 2\n")

-- adding a non-existent file should fail

check(mtn("add", "foobar"), 1, false, false)

-- newly added files should appear as such

check(mtn("add", "file0"), 0, false, true)
check(qgrep("adding 'file0'", "stderr"))

-- Default is --no-recursive
check(mtn("add", "dir"), 0, false, true)
check(not qgrep("adding 'dir/file1'", "stderr"))
check(not qgrep("adding 'dir/file2'", "stderr"))

check(mtn("add", "-R", "dir"), 0, false, true)
check(qgrep("adding 'dir/file1'", "stderr"))
check(qgrep("adding 'dir/file2'", "stderr"))

check(mtn("status"), 0, true)
check(qgrep("file0", "stdout"))
check(qgrep("file1", "stdout"))
check(qgrep("file2", "stdout"))

commit()

-- redundant additions should not appear 
-- (i.e. they should be ignored)

check(mtn("add", "file0"), 0, false, true)
check(qgrep("skipping 'file0'", "stderr"))

check(mtn("add", "-R", "dir"), 0, false, true)
check(qgrep("skipping 'dir/file1'", "stderr"))
check(qgrep("skipping 'dir/file2'", "stderr"))

check(mtn("status"), 0, true)
check(not qgrep("file0", "stdout"))
check(not qgrep("file1", "stdout"))
check(not qgrep("file2", "stdout"))

writefile("file3", "file 3\n")
writefile("dir/file5", "file 5\n")
mkdir("dir2")
writefile("dir2/file7", "file 7\n")

-- 'add --unknown --recursive' should add any files that 'ls unknown' shows.
-- Default for add is --no-recursive, for ls it is --recursive. So dir/* and dir2/* are added.
check(mtn("ls", "unknown"), 0, true, false)
check(samelines("stdout",
{"dir/file5",
 "dir2",
 "dir2/file7",
 "emptyhomedir",
 "file3",
 "min_hooks.lua",
 "stderr",
 "stdout",
 "tester.log"}))

-- Note that 'ls ignored' does not recurse into ignored directory 'keys'
-- ignored files are _not_ in .mtn-ignore; see ../test_hooks.lua ignore_file
check(mtn("ls", "ignored"), 0, true, false)
check(samelines("stdout",
{"keys",
 "test.db",
 "test_hooks.lua",
 "ts-stderr",
 "ts-stdin",
 "ts-stdout"}))
 
check(mtn("add", "--unknown", "--recursive"), 0, false, true)
check(samelines("stderr",
{"mtn: skipping ignorable file 'keys'",
 "mtn: skipping ignorable file 'test.db'",
 "mtn: skipping ignorable file 'test_hooks.lua'",
 "mtn: skipping ignorable file 'ts-stderr'",
 "mtn: skipping ignorable file 'ts-stdin'",
 "mtn: skipping ignorable file 'ts-stdout'",
 "mtn: adding 'dir/file5' to workspace manifest",
 "mtn: adding 'dir2' to workspace manifest",
 "mtn: adding 'dir2/file7' to workspace manifest",
 "mtn: adding 'emptyhomedir' to workspace manifest",
 "mtn: adding 'file3' to workspace manifest",
 "mtn: adding 'min_hooks.lua' to workspace manifest",
 "mtn: adding 'stderr' to workspace manifest",
 "mtn: adding 'stdout' to workspace manifest",
 "mtn: adding 'tester.log' to workspace manifest"}))
 
check(mtn("status"), 0, true)
check(not qgrep("file0", "stdout"))
check(not qgrep("file1", "stdout"))
check(not qgrep("file2", "stdout"))
check(qgrep("file3", "stdout"))
check(qgrep("file5", "stdout"))

commit()

check(mtn("status"), 0, true)
check(not qgrep("file0", "stdout"))
check(not qgrep("file1", "stdout"))
check(not qgrep("file2", "stdout"))
check(not qgrep("file3", "stdout"))
check(not qgrep("file5", "stdout"))

-- end of file
