-- Test reporting and resolving drop/modified conflicts
--
-- tests for the restrictions imposed by orphan, resoltion consistency
-- are in resolved_dropped_modified_3
-- 
-- other resolve_conflicts_dropped_modified_* tests validate resolving
-- in extended use cases.

-- Parent nodes can be in several states: dropped, modified,
-- recreated. Modified nodes can also be renamed, but that is
-- orthogonal to resolutions; the parent name is used to detect
-- recreate or duplicate name, and to name the result node if no
-- rename resolution is specified. Dropped files can also be orphaned,
-- but that just restricts the allowed resolutions (no keep).
-- 
-- We need to test all combinations of left/right, parent node state,
-- and resolution:
--
-- state    resolution      left file       right file
-- dropped  drop            file_3 etc      file_2 etc
--          keep            (not supported)
--          rename          (not supported)
--          user            (not supported)
--          user_rename     (not supported)
-- 
-- modified drop            file_2      file_3 etc
--          keep            file_4      file_5
--          rename          file_14     file_9
--          user            file_6      file_7
--          user_rename     file_15     file_10
-- 
-- recreated drop           file_16     file_12
--           keep           file_12     file_14
--           rename         file_17     file_15
--           user           file_13     file_16
--           user_rename    file_18     file_13
--

mtn_setup()

-- Create conflicts with single resolutions; modify and/or rename file in
-- one parent, drop in the other.
-- 
-- Six conflicts to test three possible resolutions, with drop on
-- both left and right. 

addfile("file_2", "file_2 base") -- modify/rename left: drop right: drop left, drop right
addfile("file_3", "file_3 base") -- drop left; modify/rename right: drop left, drop right
addfile("file_4", "file_4 base") -- modify left; modify, rename, and drop right; keep left, drop right
addfile("file_5", "file_5 base") -- modify, rename, and drop left; modify right; drop left, keep right
addfile("file_6", "file_6 base") -- modify/rename left, drop right; user left, drop right
addfile("file_7", "file_7 base") -- drop left, modify/rename right; drop left, user right
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
 "mtn: resolve_first rename",
 "mtn: resolve_first user_rename \"new_content_name\" \"new_file_name\"",
 "mtn: resolve_first keep",
 "mtn: resolve_first user \"name\""}))

check(mtn("conflicts", "resolve_first", "drop"), 0, nil, true)

-- check for nice error message if not all dropped_modified conflicts are resolved
-- we have to use explicit_merge to get left/right to match 'conflicts store'
check(mtn("explicit_merge", "--resolve-conflicts", left_1, right_1, "testbranch"), 1, nil, true)
check(qgrep("no resolution provided for dropped_modified 'file_3_renamed'", "stderr"))
             
check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_3_renamed'",
 "mtn: dropped on the left",
 "mtn: modified on the right",
 "mtn: possible resolutions:",
 "mtn: resolve_first drop",
 "mtn: resolve_first rename",
 "mtn: resolve_first user_rename \"new_content_name\" \"new_file_name\"",
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
 "mtn: resolve_first rename",
 "mtn: resolve_first user_rename \"new_content_name\" \"new_file_name\"",
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
 "mtn: resolve_first rename",
 "mtn: resolve_first user_rename \"new_content_name\" \"new_file_name\"",
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
 "mtn: resolve_first rename",
 "mtn: resolve_first user_rename \"new_content_name\" \"new_file_name\"",
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
 "mtn: resolve_first rename",
 "mtn: resolve_first user_rename \"new_content_name\" \"new_file_name\"",
 "mtn: resolve_first keep",
 "mtn: resolve_first user \"name\""}))

writefile("_MTN/resolutions/file_7_resolved", "file_7 resolved")
check(mtn("conflicts", "resolve_first", "user", "_MTN/resolutions/file_7_resolved"), 0, nil, true)

canonicalize("_MTN/conflicts")
check(samefilestd("conflicts-resolved", "_MTN/conflicts"))

-- we have to use explicit_merge to get left/right to match 'conflicts store'
check(mtn("explicit_merge", "--resolve-conflicts", left_1, right_1, "testbranch"), 0, nil, true)
check(samelines("stderr",
{"mtn: [left]  7b2ef4343b0717bcd122498a1a0b7ff7acffb64c",
 "mtn: [right] ca7922b510f9daf5c4b28c6788315ee82eb9a7f0",
 "mtn: dropping 'file_2_renamed' from left",
 "mtn: dropping 'file_3_renamed' from right",
 "mtn: keeping 'file_4' from left",
 "mtn: history for 'file_4' from left will be lost; see user manual Merge Conflicts section",
 "mtn: keeping 'file_5' from right",
 "mtn: history for 'file_5' from right will be lost; see user manual Merge Conflicts section",
 "mtn: replacing content of 'file_6_renamed' from left with '_MTN/resolutions/file_6_resolved'",
 "mtn: history for 'file_6_renamed' from left will be lost; see user manual Merge Conflicts section",
 "mtn: replacing content of 'file_7_renamed' from right with '_MTN/resolutions/file_7_resolved'",
 "mtn: history for 'file_7_renamed' from right will be lost; see user manual Merge Conflicts section",
 "mtn: [merged] 57bf835ef0434411189dc3eca1650a6bba513c14"}))

-- If a file is renamed (without other change) and dropped,
-- the rename is ignored:

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
-- We create four potential conflicts; one ignored, three with different resolutions:

adddir("dir1") -- empty - drop left; rename right (not a conflict; just dropped)
mkdir("dir2")  -- not empty - modified left; drop right
addfile("dir2/file_9", "file_9 base") -- resolution: rename left, drop right
addfile("dir2/file_10", "file_10 base") -- resolution: user_rename left, drop right
addfile("dir2/file_11", "file_11 base") -- resolution: drop left, drop right
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

-- Test error message from invalid resolution
check(mtn("conflicts", "resolve_first", "keep"), 1, nil, true)
check(samelines("stderr",
{"mtn: misuse: orphaned files must be renamed"}))

check(mtn("conflicts", "resolve_first", "user", "foo"), 1, nil, true)
check(samelines("stderr",
{"mtn: misuse: orphaned files must be renamed"}))

check(mtn("conflicts", "resolve_first", "drop"), 0, nil, nil)

check(mtn("conflicts", "resolve_first", "rename", "file_9"), 0, nil, nil)

check(samefilestd("conflicts-orphaned-resolved", "_MTN/conflicts"))

check(mtn("explicit_merge", "--resolve-conflicts", left_3, right_3, "testbranch"), 0, nil, true)
check(samelines("stderr",
{"mtn: [left]  4228fbd8003cdd89e7eea51fcef10c3f91d78f69",
 "mtn: [right] 6cb6438a490a1ad4c69ff6cac23c75a903cd9cfd",
 "mtn: replacing content of 'dir2/file_10' from left with '_MTN/resolutions/file_10'",
 "mtn: history for 'dir2/file_10' from left will be lost; see user manual Merge Conflicts section",
 "mtn: renaming 'dir2/file_10' from left to 'file_10'",
 "mtn: dropping 'dir2/file_11' from left",
 "mtn: renaming 'dir2/file_9' from left to 'file_9'",
 "mtn: history for 'dir2/file_9' from left will be lost; see user manual Merge Conflicts section",
 "mtn: [merged] 5cafe5405ed31c81f9061be62e38f25aeaaea9c5"}))

check(mtn("update"), 0, nil, true)

-- Test recreated; drop then re-add vs modify. This used to be the test
-- "merge((patch_a),_(drop_a,_add_a))"
addfile("file_12", "file_12 base") -- modify in left; drop, add in right: keep left, drop right
addfile("file_13", "file_13 base") -- drop, add in left; modify in right: user left, user_rename right

-- Other cases not covered above
addfile("file_14", "file_14 base") -- modify in left; recreated in right: rename left, keep right
addfile("file_15", "file_15 base") -- modify in left; recreated in right: user_rename left, rename right
addfile("file_16", "file_16 base") -- recreated in left; modify in right: drop left, rename right
addfile("file_17", "file_17 base") -- recreated in left; modify in right: rename left, drop right
addfile("file_18", "file_18 base") -- recreated in left; modify in right: user_rename left, drop right
commit("testbranch", "base 4")
base_4 = base_revision()

writefile("file_12", "file_12 left")
check(mtn("drop", "file_13"), 0, false, false)

writefile("file_14", "file_14 left")
writefile("file_15", "file_15 left")

check(mtn("drop", "file_16"), 0, false, false)
check(mtn("drop", "file_17"), 0, false, false)
check(mtn("drop", "file_18"), 0, false, false)
commit("testbranch", "left 4a")

addfile("file_13", "file_13 left re-add")
addfile("file_16", "file_16 left re-add")
addfile("file_17", "file_17 left re-add")
addfile("file_18", "file_18 left re-add")
commit("testbranch", "left 4b")
left_4 = base_revision()

revert_to(base_4)

check(mtn("drop", "file_12"), 0, false, false)
writefile("file_13", "file_13 right")
check(mtn("drop", "file_14"), 0, false, false)
check(mtn("drop", "file_15"), 0, false, false)
writefile("file_16", "file_16 right")
writefile("file_17", "file_17 right")
writefile("file_18", "file_18 right")
commit("testbranch", "right 4a")

addfile("file_12", "file_12 right re-add")
addfile("file_14", "file_14 right re-add")
addfile("file_15", "file_15 right re-add")
commit("testbranch", "right 4b")
right_4 = base_revision()

check(mtn("show_conflicts", left_4, right_4), 0, nil, true)
check(samelines("stderr",
{"mtn: [left]     " .. left_4,
 "mtn: [right]    " .. right_4,
 "mtn: [ancestor] " .. base_4,
 "mtn: conflict: file 'file_12'",
 "mtn: modified on the left, named file_12",
 "mtn: dropped and recreated on the right",
 "mtn: conflict: file 'file_13'",
 "mtn: dropped and recreated on the left",
 "mtn: modified on the right, named file_13",
 "mtn: conflict: file 'file_14'",
 "mtn: modified on the left, named file_14",
 "mtn: dropped and recreated on the right",
 "mtn: conflict: file 'file_15'",
 "mtn: modified on the left, named file_15",
 "mtn: dropped and recreated on the right",
 "mtn: conflict: file 'file_16'",
 "mtn: dropped and recreated on the left",
 "mtn: modified on the right, named file_16",
 "mtn: conflict: file 'file_17'",
 "mtn: dropped and recreated on the left",
 "mtn: modified on the right, named file_17",
 "mtn: conflict: file 'file_18'",
 "mtn: dropped and recreated on the left",
 "mtn: modified on the right, named file_18",
 "mtn: 7 conflicts with supported resolutions."}))

check(mtn("conflicts", "store", left_4, right_4), 0, nil, true)
check(samefilestd("conflicts-recreated", "_MTN/conflicts"))

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_12'",
 "mtn: modified on the left",
 "mtn: dropped and recreated on the right",
 "mtn: possible resolutions:",
 "mtn: resolve_first_left drop",
 "mtn: resolve_first_left rename",
 "mtn: resolve_first_left user_rename \"new_content_name\" \"new_file_name\"",
 "mtn: resolve_first_left keep",
 "mtn: resolve_first_left user \"name\"",
 "mtn: resolve_first_right drop",
 "mtn: resolve_first_right rename",
 "mtn: resolve_first_right user_rename \"new_content_name\" \"new_file_name\"",
 "mtn: resolve_first_right keep",
 "mtn: resolve_first_right user \"name\""}))

-- need to specify both left and right resolutions
check(mtn("conflicts", "resolve_first", "drop"), 1, nil, true)
check(samelines("stderr",
{"mtn: misuse: must specify 'resolve_first_left' or 'resolve_first_right' (not just 'resolve_first')"}))

check(mtn("conflicts", "resolve_first_left", "keep"), 0, nil, nil)
check(mtn("conflicts", "resolve_first_right", "drop"), 0, nil, nil)

check(mtn("conflicts", "show_first"), 0, nil, true)
qgrep("mtn: conflict: file 'file_13'", "stderr")
qgrep("mtn: dropped and recreated on the left", "stderr")
qgrep("mtn: modified on the right", "stderr")

mkdir("_MTN")
mkdir("_MTN/resolutions")
writefile("_MTN/resolutions/file_13", "file_13 user")
check(mtn("conflicts", "resolve_first_left", "user", "_MTN/resolutions/file_13"), 0, nil, nil)
check(mtn("conflicts", "resolve_first_right", "drop"), 0, nil, nil)

check(mtn("conflicts", "resolve_first_left", "rename", "file_14_renamed"), 0, nil, nil)
check(mtn("conflicts", "resolve_first_right", "keep"), 0, nil, nil)

writefile("_MTN/resolutions/file_15_left", "file_15 user left")
check(mtn("conflicts", "resolve_first_left", "user_rename", "_MTN/resolutions/file_15_left", "file_15_renamed_left"), 0, nil, nil)
check(mtn("conflicts", "resolve_first_right", "rename", "file_15_renamed_right"), 0, nil, nil)

check(mtn("conflicts", "resolve_first_left", "drop"), 0, nil, nil)
check(mtn("conflicts", "resolve_first_right", "rename", "file_16_renamed"), 0, nil, nil)

check(mtn("conflicts", "resolve_first_left", "rename", "file_17_renamed"), 0, nil, nil)
check(mtn("conflicts", "resolve_first_right", "drop"), 0, nil, nil)

mkdir("_MTN")
mkdir("_MTN/resolutions")
writefile("_MTN/resolutions/file_18", "file_18 user")
check(mtn("conflicts", "resolve_first_left", "user_rename", "_MTN/resolutions/file_18", "file_18_renamed"), 0, nil, nil)
check(mtn("conflicts", "resolve_first_right", "drop"), 0, nil, nil)

check(samefilestd("conflicts-recreated-resolved", "_MTN/conflicts"))

check(mtn("explicit_merge", "--resolve-conflicts", left_4, right_4, "testbranch"), 0, nil, true)
check(samelines("stderr",
{"mtn: [left]  " .. left_4,
 "mtn: [right] " .. right_4,
 "mtn: keeping 'file_12' from left",
 "mtn: history for 'file_12' from left will be lost; see user manual Merge Conflicts section",
 "mtn: dropping 'file_12' from right",
 "mtn: replacing content of 'file_13' from left with '_MTN/resolutions/file_13'",
 "mtn: dropping 'file_13' from right",
 "mtn: renaming 'file_14' from left to 'file_14_renamed'",
 "mtn: history for 'file_14' from left will be lost; see user manual Merge Conflicts section",
 "mtn: keeping 'file_14' from right",
 "mtn: replacing content of 'file_15' from left with '_MTN/resolutions/file_15_left'",
 "mtn: history for 'file_15' from left will be lost; see user manual Merge Conflicts section",
 "mtn: renaming 'file_15' from left to 'file_15_renamed_left'",
 "mtn: renaming 'file_15' from right to 'file_15_renamed_right'",
 "mtn: dropping 'file_16' from left",
 "mtn: renaming 'file_16' from right to 'file_16_renamed'",
 "mtn: history for 'file_16' from right will be lost; see user manual Merge Conflicts section",
 "mtn: renaming 'file_17' from left to 'file_17_renamed'",
 "mtn: dropping 'file_17' from right",
 "mtn: replacing content of 'file_18' from left with '_MTN/resolutions/file_18'",
 "mtn: renaming 'file_18' from left to 'file_18_renamed'",
 "mtn: dropping 'file_18' from right",
 "mtn: [merged] c6d6dba528110b6aa32572f6939982a1d56b17e0"}))

check(mtn("update"), 0, nil, true)
check(samelines("file_12", {"file_12 left"}))
check(samelines("file_13", {"file_13 user"}))
check(samelines("file_14_renamed", {"file_14 left"}))
check(samelines("file_14", {"file_14 right re-add"}))
check(samelines("file_15_renamed_left", {"file_15 user left"}))
check(samelines("file_15_renamed_right", {"file_15 right re-add"}))
check(samelines("file_16_renamed", {"file_16 right"}))
check(samelines("file_17_renamed", {"file_17 left re-add"}))
check(samelines("file_18_renamed", {"file_18 user"}))

-- end of file
