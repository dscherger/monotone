-- Test resolving orphaned_file and orphaned_directory conflicts

mtn_setup()

addfile("foo", "foo base")
mkdir("stuff")
addfile("stuff/file1", "file1 1")
commit("testbranch", "base")
base = base_revision()

check(mtn("rm", "stuff/file1"), 0, false, false)
check(mtn("rm", "stuff"), 0, false, false)
commit("testbranch", "right 1")
right_1 = base_revision()

revert_to(base)

addfile("stuff/file2", "file2 1")
addfile("stuff/file3", "file3 1")
mkdir("stuff/dir1")
addfile("stuff/dir1/file4", "file4")
mkdir("stuff/dir2")
addfile("stuff/dir2/file5", "file5")
mkdir("stuff/dir3")
check(mtn("add", "stuff/dir3"), 0, nil, false)
commit("testbranch", "left 1")
left_1 = base_revision()

check(mtn("conflicts", "store"), 0, nil, nil)

-- Check suggested resolutions for orphaned directory
check(mtn("conflicts", "show_first"), 0, nil, true)
check(
"mtn: orphaned node stuff/dir1\n" ..
"mtn: possible resolutions:\n" ..
"mtn: resolve_first drop\n" ..
"mtn: resolve_first rename \"file_name\"\n" == readfile("stderr"))

-- stuff/dir1 => dir1
check(mtn("conflicts", "resolve_first", "rename", "dir1"), 0, nil, nil)

-- stuff/dir2 => dropped (later gives error due to not empty)
check(mtn("conflicts", "show_first"), 0, nil, true)
check(qgrep("orphaned node stuff/dir2", "stderr"));
check(mtn("conflicts", "resolve_first", "drop"), 0, nil, nil)

-- stuff/dir3 => dropped
check(mtn("conflicts", "show_first"), 0, nil, true)
check(qgrep("orphaned node stuff/dir3", "stderr"));
check(mtn("conflicts", "resolve_first", "drop"), 0, nil, nil)

-- Check suggested resolutions for orphaned file
check(mtn("conflicts", "show_first"), 0, nil, true)
check(
"mtn: orphaned node stuff/file2\n" ..
"mtn: possible resolutions:\n" ..
"mtn: resolve_first drop\n" ..
"mtn: resolve_first rename \"file_name\"\n" == readfile("stderr"))

-- stuff/file2 => file2
check(mtn("conflicts", "resolve_first", "rename", "file2"), 0, nil, nil)

-- stuff/file3 => dropped
check(mtn("conflicts", "show_first"), 0, nil, true)
check(qgrep("orphaned node stuff/file3", "stderr"));
check(mtn("conflicts", "resolve_first", "drop"), 0, nil, nil)

check(mtn("merge", "--resolve-conflicts"), 1, nil, true)
check(qgrep("directory stuff/dir2; it is not empty", "stderr"));

check(mtn("drop", "stuff/dir2/file5"), 0, nil, false)
commit("testbranch", "right 2")
right_2 = base_revision()

--  and now we start the resolution process over again
check(mtn("conflicts", "store"), 0, nil, nil)
check(mtn("conflicts", "show_first"), 0, nil, true)
check(qgrep("orphaned node stuff/dir1", "stderr"));
check(mtn("conflicts", "resolve_first", "rename", "dir1"), 0, nil, nil)

check(mtn("conflicts", "show_first"), 0, nil, true)
check(qgrep("orphaned node stuff/dir2", "stderr"));
check(mtn("conflicts", "resolve_first", "drop"), 0, nil, nil)

check(mtn("conflicts", "show_first"), 0, nil, true)
check(qgrep("orphaned node stuff/dir3", "stderr"));
check(mtn("conflicts", "resolve_first", "drop"), 0, nil, nil)

check(mtn("conflicts", "show_first"), 0, nil, true)
check(qgrep("orphaned node stuff/file2", "stderr"));
check(mtn("conflicts", "resolve_first", "rename", "file2"), 0, nil, nil)

check(mtn("conflicts", "show_first"), 0, nil, true)
check(qgrep("orphaned node stuff/file3", "stderr"));
check(mtn("conflicts", "resolve_first", "drop"), 0, nil, nil)

check(mtn("merge", "--resolve-conflicts"), 0, nil, true)
check(mtn("update"), 0, nil, true)
check(exists("file2"))
check(not exists("file3"))
check(exists("dir1"))
check(not exists("dir2"))
check(not exists("stuff"))

-- end of file
