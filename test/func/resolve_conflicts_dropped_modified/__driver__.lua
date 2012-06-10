-- Test reporting and resolving drop/modified conflicts
--
-- other resolve_conflicts_dropped_modified_* tests validate resolving
-- in extended use cases.

mtn_setup()

-- Create conflicts; modify and rename file in one head, drop in
-- other.
-- 
-- Six conflicts to test the three possible resolutions, with drop on
-- both left and right. Number in file name is the node number (helps
-- in debugging; node 1 is the root directory).
--
-- The case of a modified file in a dropped directory is tested below.

addfile("file_2", "file_2 base") -- modify/rename left, drop right; drop
addfile("file_3", "file_3 base") -- drop left, modify/rename right; drop
addfile("file_4", "file_4 base") -- modify left; modify, rename, and drop right; keep
addfile("file_5", "file_5 base") -- modify, rename, and drop left; modify right; keep
addfile("file_6", "file_6 base") -- modify/rename left, drop right; user
addfile("file_7", "file_7 base") -- drop left, modify/rename right; user
commit("testbranch", "base")
base = base_revision()

writefile("file_2", "file_2 left")
check(mtn("mv", "file_2", "file_2_renamed"), 0, false, false)

check(mtn("drop", "file_3"), 0, false, false)

writefile("file_4", "file_4 left")

writefile("file_5", "file_5 left")
check(mtn("mv", "file_5", "file_5_renamed"), 0, false, false)

writefile("file_6", "file_6 left")
check(mtn("mv", "file_6", "file_6_renamed"), 0, false, false)

check(mtn("drop", "file_7"), 0, false, false)

commit("testbranch", "left 1a")

check(mtn("drop", "file_5_renamed"), 0, nil, true)
commit("testbranch", "left 1b")
left_1 = base_revision()

revert_to(base)

check(mtn("drop", "file_2"), 0, false, false)

writefile("file_3", "file_3 right")
check(mtn("mv", "file_3", "file_3_renamed"), 0, false, false)

writefile("file_4", "file_4 right")
check(mtn("mv", "file_4", "file_4_renamed"), 0, false, false)

writefile("file_5", "file_5 right")

check(mtn("drop", "file_6"), 0, false, false)

writefile("file_7", "file_7 right")
check(mtn("mv", "file_7", "file_7_renamed"), 0, false, false)

commit("testbranch", "right 1a")

check(mtn("drop", "file_4_renamed"), 0, false, false)
commit("testbranch", "right 1b")
right_1 = base_revision()

-- Now start the conflict resolution process. First show the conflicts.
check(mtn("show_conflicts", left_1, right_1), 0, nil, true)
canonicalize("stderr")
check(samefilestd("show_conflicts", "stderr"))

check(mtn("automate", "show_conflicts", left_1, right_1), 0, true, nil)
canonicalize("stdout")
check(samefilestd("conflicts", "stdout"))

-- Now store and resolve them one by one.
check(mtn("conflicts", "store", left_1, right_1), 0, nil, true)
check(samelines("stderr",
{"mtn: 6 conflicts with supported resolutions.",
 "mtn: stored in '_MTN/conflicts'"}))

canonicalize("_MTN/conflicts")
check(samefilestd("conflicts", "_MTN/conflicts"))

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_2_renamed'",
 "mtn: modified on the left",
 "mtn: dropped on the right",
 "mtn: possible resolutions:",
 "mtn: resolve_first drop",
 "mtn: resolve_first keep",
 "mtn: resolve_first user \"name\""}))

check(mtn("conflicts", "resolve_first", "drop"), 0, nil, true)

-- check for nice error message if not all dropped_modified conflicts are resolved
-- we have to use explicit_merge to get left/right to match 'conflicts store'
check(mtn("explicit_merge", "--resolve-conflicts", left_1, right_1, "testbranch"), 1, nil, true)
check(qgrep("no resolution provided for", "stderr"))
             
check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_3_renamed'",
 "mtn: dropped on the left",
 "mtn: modified on the right",
 "mtn: possible resolutions:",
 "mtn: resolve_first drop",
 "mtn: resolve_first keep",
 "mtn: resolve_first user \"name\""}))

check(mtn("conflicts", "resolve_first", "drop"), 0, nil, true)

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_4'",
 "mtn: modified on the left",
 "mtn: dropped on the right",
 "mtn: possible resolutions:",
 "mtn: resolve_first drop",
 "mtn: resolve_first keep",
 "mtn: resolve_first user \"name\""}))

check(mtn("conflicts", "resolve_first", "keep"), 0, nil, true)

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_5'",
 "mtn: dropped on the left",
 "mtn: modified on the right",
 "mtn: possible resolutions:",
 "mtn: resolve_first drop",
 "mtn: resolve_first keep",
 "mtn: resolve_first user \"name\""}))

check(mtn("conflicts", "resolve_first", "keep"), 0, nil, true)

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_6_renamed'",
 "mtn: modified on the left",
 "mtn: dropped on the right",
 "mtn: possible resolutions:",
 "mtn: resolve_first drop",
 "mtn: resolve_first keep",
 "mtn: resolve_first user \"name\""}))

mkdir("_MTN/resolutions")
writefile("_MTN/resolutions/file_6_resolved", "file_6 resolved")
check(mtn("conflicts", "resolve_first", "user", "_MTN/resolutions/file_6_resolved"), 0, nil, true)

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_7_renamed'",
 "mtn: dropped on the left",
 "mtn: modified on the right",
 "mtn: possible resolutions:",
 "mtn: resolve_first drop",
 "mtn: resolve_first keep",
 "mtn: resolve_first user \"name\""}))

mkdir("_MTN/resolutions")
writefile("_MTN/resolutions/file_7_resolved", "file_7 resolved")
check(mtn("conflicts", "resolve_first", "user", "_MTN/resolutions/file_7_resolved"), 0, nil, true)

canonicalize("_MTN/conflicts")
check(samefilestd("conflicts-resolved", "_MTN/conflicts"))

-- we have to use explicit_merge to get left/right to match 'conflicts store'
check(mtn("explicit_merge", "--resolve-conflicts", left_1, right_1, "testbranch"), 0, nil, true)
check(qgrep("dropping 'file_2_renamed'", "stderr"))
check(qgrep("dropping 'file_3_renamed'", "stderr"))
check(qgrep("keeping 'file_4'", "stderr"))
check(qgrep("keeping 'file_5'", "stderr"))
check(qgrep("replacing content of 'file_6_renamed' with '_MTN/resolutions/file_6_resolved", "stderr"))
check(qgrep("replacing content of 'file_7_renamed' with '_MTN/resolutions/file_7_resolved", "stderr"))
check(not qgrep("warning", "stderr"))

-- If a file is renamed (without other change) and dropped,
-- the change is ignored:

addfile("file_8", "file_8 base") -- rename left, drop right
commit("testbranch", "base 2")
base_2 = base_revision()

check(mtn("mv", "file_8", "file_8_renamed"), 0, false, false)
commit("testbranch", "left 2")
left_2 = base_revision()

revert_to(base_2)

check(mtn("drop", "file_8"), 0, false, false)
commit("testbranch", "right 2")
right_2 = base_revision()

check(mtn("show_conflicts", left_2, right_2), 0, nil, true)
check(qgrep("0 conflicts", "stderr"))

-- There is no such thing as a dropped/modified directory; if the
-- directory is empty, the only possible change is rename, which is
-- ignored.
--
-- If the directory is not empty, that creates a dropped/modified file
-- conflict (not an orphaned file conflict, although that would also
-- make sense). This used to be the test
-- "(imp)_merge((patch_foo_a),_(delete_foo_))"
--
-- We create three potential conflicts; one ignored, three with different resolutions:

adddir("dir1") -- empty, dropped and renamed (not a conflict; just dropped)
mkdir("dir2")  -- not empty, dropped, contents modified
addfile("dir2/file_9", "file_9 base") -- resolved by rename
addfile("dir2/file_10", "file_10 base") -- resolved by user_rename
addfile("dir2/file_11", "file_11 base") -- resolved by drop
commit("testbranch", "base 3")
base_3 = base_revision()

check(mtn("mv", "dir1", "dir3"), 0, false, false)

writefile("dir2/file_9", "file_9 left")
writefile("dir2/file_10", "file_10 left")
writefile("dir2/file_11", "file_11 left")
commit("testbranch", "left 3")
left_3 = base_revision()

revert_to(base_3)

check(mtn("drop", "dir1", "--no-recursive"), 0, false, false)

check(mtn("drop", "--recursive", "dir2"), 0, false, false)

commit("testbranch", "right 3")
right_3 = base_revision()

check(mtn("show_conflicts", left_3, right_3), 0, nil, true)
canonicalize("stderr")
check(samefilestd("show_conflicts-orphaned", "stderr"))

-- Show these conflicts can be resolved
check(mtn("conflicts", "store", left_3, right_3), 0, nil, true)

canonicalize("_MTN/conflicts")
check(samefilestd("conflicts-orphaned", "_MTN/conflicts"))

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'dir2/file_10'",
 "mtn: modified on the left",
 "mtn: orphaned on the right",
 "mtn: possible resolutions:",
 "mtn: resolve_first drop",
 "mtn: resolve_first rename",
 "mtn: resolve_first user_rename \"new_content_name\" \"new_file_name\""}))

mkdir("_MTN")
mkdir("_MTN/resolutions")
writefile("_MTN/resolutions/file_10", "file_10 user")
check(mtn("conflicts", "resolve_first", "user_rename", "_MTN/resolutions/file_10", "file_10"), 0, nil, true)

check(mtn("conflicts", "resolve_first", "drop"), 0, nil, nil)

check(mtn("conflicts", "resolve_first", "rename", "file_9"), 0, nil, nil)

check(samefilestd("conflicts-orphaned-resolved", "_MTN/conflicts"))

check(mtn("explicit_merge", "--resolve-conflicts", left_3, right_3, "testbranch"), 0, nil, true)
check(samelines("stderr",
{"mtn: [left]  4228fbd8003cdd89e7eea51fcef10c3f91d78f69",
 "mtn: [right] 6cb6438a490a1ad4c69ff6cac23c75a903cd9cfd",
 "mtn: replacing content of 'dir2/file_10' (renamed to 'file_10') with '_MTN/resolutions/file_10'",
 "mtn: dropping 'dir2/file_11'",
 "mtn: renaming 'dir2/file_9' to 'file_9'",
 "mtn: [merged] 5cafe5405ed31c81f9061be62e38f25aeaaea9c5"}))
 
-- A special case; drop then re-add vs modify. This used to be the test
-- "merge((patch_a),_(drop_a,_add_a))"
addfile("file_10", "file_10 base") -- modify in left; drop, add in right
addfile("file_11", "file_11 base") -- drop, add in left; modify in right
commit("testbranch", "base 4")
base_4 = base_revision()

writefile("file_10", "file_10 left")

check(mtn("drop", "file_11"), 0, false, false)
commit("testbranch", "left 4a")

addfile("file_11", "file_11 left re-add")
commit("testbranch", "left 4b")
left_4 = base_revision()

revert_to(base_4)

check(mtn("drop", "file_10"), 0, false, false)
writefile("file_11", "file_11 right")
commit("testbranch", "right 4a")

addfile("file_10", "file_10 right re-add")
commit("testbranch", "right 4b")
right_4 = base_revision()

check(mtn("show_conflicts", left_4, right_4), 0, nil, true)
check(samelines("stderr",
{"mtn: [left]     9485fe891d5e23d6dc30140228cd02840ee719e9",
 "mtn: [right]    9a8192d3bf263cbd5782791e823b837d42af6902",
 "mtn: [ancestor] 209e4118bda3960b2f83e48b2368e981ab748ee5",
 "mtn: conflict: file 'file_10' from revision 209e4118bda3960b2f83e48b2368e981ab748ee5",
 "mtn: modified on the left, named file_10",
 "mtn: dropped and recreated on the right",
 "mtn: conflict: file 'file_11' from revision 209e4118bda3960b2f83e48b2368e981ab748ee5",
 "mtn: dropped and recreated on the left",
 "mtn: modified on the right, named file_11",
 "mtn: 2 conflicts with supported resolutions."}))

check(mtn("conflicts", "store", left_4, right_4), 0, nil, true)
check(samefilestd("conflicts-recreated", "_MTN/conflicts"))

-- drop is not a valid resolution in this case
check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_10'",
 "mtn: modified on the left",
 "mtn: dropped and recreated on the right",
 "mtn: possible resolutions:",
 "mtn: resolve_first keep",
 "mtn: resolve_first user \"name\""}))

check(mtn("conflicts", "resolve_first", "drop"), 1, nil, true)
check(samelines("stderr", {"mtn: misuse: recreated files may not be dropped"}))

check(mtn("conflicts", "resolve_first", "keep"), 0, nil, nil)

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_11'",
 "mtn: dropped and recreated on the left",
 "mtn: modified on the right",
 "mtn: possible resolutions:",
 "mtn: resolve_first keep",
 "mtn: resolve_first user \"name\""}))

mkdir("_MTN")
mkdir("_MTN/resolutions")
writefile("_MTN/resolutions/file_11", "file_11 user")
check(mtn("conflicts", "resolve_first", "user", "_MTN/resolutions/file_11"), 0, nil, nil)

check(samefilestd("conflicts-recreated-resolved", "_MTN/conflicts"))

check(mtn("explicit_merge", "--resolve-conflicts", left_4, right_4, "testbranch"), 0, nil, true)
check(samelines("stderr",
{"mtn: [left]  9485fe891d5e23d6dc30140228cd02840ee719e9",
 "mtn: [right] 9a8192d3bf263cbd5782791e823b837d42af6902",
 "mtn: keeping 'file_10' from left",
 "mtn: replacing content of 'file_11' with '_MTN/resolutions/file_11'",
 "mtn: [merged] 306eb31064512a8a2f4d316ff7a7ec32a1f64f4c"}))

check(mtn("update"), 0, nil, true)
check(samelines("file_10", {"file_10 left"}))

-- end of file
