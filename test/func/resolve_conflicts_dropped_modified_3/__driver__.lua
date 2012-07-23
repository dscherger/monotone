-- Test enforcement of consistent resolutions for drop/modified conflicts.
--
-- Only invalid combinations are tested here (we verify a good error
-- message); valid combinations are tested in
-- resolve_conflicts_dropped_modified_1.
--
-- The left and right conflicts chosen by the user must be consistent;
-- they must give different names for the two sides. 
--
-- When one file is in the dropped state, only one resolution can be
-- specified; that of the modified file.
--
-- Rename on both sides is valid, unless the user specifies the same
-- new name for both; that is tested only once here.
--
-- The only inconsistent cases are between modified and recreated
-- files. A recreated file is detected by having the same name as the
-- modified file; if the modified file has also been renamed, the
-- recreated file must have the same name as the renamed file. Thus we
-- do not need to consider a renamed modified file as a separate case.
--
-- Orphaned file resolution cannot be keep or user; those error
-- messages are tested in resolve_conflicts_dropped_modified_1.
--
-- We need to test all invalid combinations of left/right resolution:
--
--      left                    right
-- state    resolution      state       resolution  file    case
-----------------------------------------------------------
-- dropped  -               dropped     -           (not a conflict)
--          -               modified    keep        (valid)
--          -               modified    rename      (valid)
--          -               modified    user        (valid)
--          -               modified    user_rename (valid)
--          -               recreated   -           (not a conflict)
--
-- modified drop            dropped     -           (valid)
--          keep            dropped     -           (valid)
--          rename          dropped     -           (valid)
--          user            dropped     -           (valid)
--          user_rename     dropped     -           (valid)
--
-- modified -               modified    -           (file content conflict)
--          drop            recreated   drop        (valid)
--          drop            recreated   keep        (valid)
--          drop            recreated   rename      (valid)
--          drop            recreated   user        (valid)
--          drop            recreated   user_rename (valid)
--          keep            recreated   drop        (valid)
--          keep            recreated   keep        file_2  1
--          keep            recreated   rename      (valid)
--          keep            recreated   user        file_2  2
--          keep            recreated   user_rename (valid)
--          rename          recreated   drop        (valid)
--          rename          recreated   keep        (valid)
--          rename          recreated   rename      (valid)
--          rename          recreated   user        (valid)
--          rename          recreated   user_rename (valid)
--          user            recreated   drop        (valid)
--          user            recreated   keep        file_4  3
--          user            recreated   rename      (valid) 
--          user            recreated   user        file_4  4
--          user            recreated   user_rename (valid)
--          user_rename     recreated   drop        (valid)
--          user_rename     recreated   keep        (valid)
--          user_rename     recreated   rename      (valid)
--          user_rename     recreated   user        (valid)
--          user_rename     recreated   user_rename (valid)
-- 
-- recreated drop           dropped     -           (valid)
--           keep           dropped     -           (valid)
--           rename         dropped     -           (valid)
--           user           dropped     -           (valid)
--           user_rename    dropped     -           (valid)
--           drop           modified    drop        (valid)
--           drop           modified    keep        (valid)
--           drop           modified    rename      (valid)
--           drop           modified    user        (valid)
--           drop           modified    user_rename (valid)
--           keep           modified    drop        (valid)
--           keep           modified    keep        file_3  5
--           keep           modified    rename      (valid)
--           keep           modified    user        file_3  6
--           keep           modified    user_rename (valid)
--           rename         modified    drop        (valid)
--           rename         modified    keep        (valid)
--           rename         modified    rename      (valid)
--           rename         modified    user        (valid)
--           rename         modified    user_rename (valid)
--           user           modified    drop        (valid)
--           user           modified    keep        file_3  7
--           user           modified    rename      (valid)
--           user           modified    user        file_3  8 
--           user           modified    user_rename (valid)
--           user_rename    modified    drop        (valid)
--           user_rename    modified    keep        (valid)
--           user_rename    modified    rename      (valid)
--           user_rename    modified    user        (valid)
--           user_rename    modified    user_rename (valid)

mtn_setup()

-- Create the test files

addfile("file_2", "file_2 base") -- modified left, recreated right
addfile("file_3", "file_3 base") -- recreated left, modified right
commit("testbranch", "base")
base = base_revision()

writefile("file_2", "file_2 left")
check(mtn("drop", "file_3"), 0, false, false)
commit("testbranch", "left 1a")

addfile("file_3", "file_3 left recreated")
commit("testbranch", "left 1b")
left_1 = base_revision()

revert_to(base)

check(mtn("drop", "file_2"), 0, false, false)
writefile("file_3", "file_3 right")
commit("testbranch", "right 1a")

addfile("file_2", "file_2 right recreated")
commit("testbranch", "right 1b")
right_1 = base_revision()

-- Store and show inconsistency error messages
check(mtn("conflicts", "store", left_1, right_1), 0, nil, true)
check(samelines("stderr",
{"mtn: 2 conflicts with supported resolutions.",
 "mtn: stored in '_MTN/conflicts'"}))

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_2'",
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

-- case 1, 2; keep *
check(mtn("conflicts", "resolve_first_left", "keep"), 0, nil, false)

-- check that inconsistent resolutions for right are not displayed
check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_2'",
 "mtn: modified on the left",
 "mtn: dropped and recreated on the right",
 "mtn: left_resolution: keep",
 "mtn: possible resolutions:",
 "mtn: resolve_first_right drop",
 "mtn: resolve_first_right rename",
 "mtn: resolve_first_right user_rename \"new_content_name\" \"new_file_name\""}))

-- check for errors from inconsistent resolutions

-- case 1: keep, keep
check(mtn("conflicts", "resolve_first_right", "keep"), 1, nil, true)
check(samelines("stderr",
{"mtn: misuse: other resolution is keep; specify 'drop', 'rename', or 'user_rename'"}))

-- case 2: keep, user
check(mtn("conflicts", "resolve_first_right", "user", "_MTN/resolutions/file_2"), 1, nil, true)
check(samelines("stderr",
{"mtn: misuse: other resolution is keep; specify 'drop', 'rename', or 'user_rename'"}))

-- case 1, 2, but specify right resolution first
check(mtn("conflicts", "store", left_1, right_1), 0, nil, true)
check(mtn("conflicts", "resolve_first_right", "keep"), 0, nil, false)
check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_2'",
 "mtn: modified on the left",
 "mtn: dropped and recreated on the right",
 "mtn: right_resolution: keep",
 "mtn: possible resolutions:",
 "mtn: resolve_first_left drop",
 "mtn: resolve_first_left rename",
 "mtn: resolve_first_left user_rename \"new_content_name\" \"new_file_name\""}))
check(mtn("conflicts", "resolve_first_left", "keep"), 1, nil, true)
check(samelines("stderr",
{"mtn: misuse: other resolution is keep; specify 'drop', 'rename', or 'user_rename'"}))

-- No error if specify right again, but it actually sets file_3 right resolution. so we have to reset
check(mtn("conflicts", "store", left_1, right_1), 0, nil, true)
check(mtn("conflicts", "resolve_first_right", "user", "_MTN/resolutions/file_2"), 0, nil, false)
check(mtn("conflicts", "show_first"), 0, nil, true)
check(qgrep("right_resolution: content_user, content: '_MTN/resolutions/file_2'", "stderr"))

check(mtn("conflicts", "resolve_first_left", "keep"), 1, nil, true)
check(samelines("stderr",
{"mtn: misuse: other resolution is content_user; specify 'drop', 'rename', or 'user_rename'"}))

-- provide a valid resolution for file_2 so file_3 is first
check(mtn("conflicts", "resolve_first_left", "drop"), 0, nil, nil)

-- case 3, 4; user *
check(mtn("conflicts", "resolve_first_left", "user", "_MTN/resolutions/file_3"), 0, nil, false)

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_3'",
 "mtn: dropped and recreated on the left",
 "mtn: modified on the right",
 "mtn: left_resolution: content_user, content: '_MTN/resolutions/file_3'",
 "mtn: possible resolutions:",
 "mtn: resolve_first_right drop",
 "mtn: resolve_first_right rename",
 "mtn: resolve_first_right user_rename \"new_content_name\" \"new_file_name\""}))

-- case 3: user, keep
check(mtn("conflicts", "resolve_first_right", "keep"), 1, nil, true)
check(samelines("stderr",
{"mtn: misuse: other resolution is content_user; specify 'drop', 'rename', or 'user_rename'"}))

-- case 4: user, user
check(mtn("conflicts", "resolve_first_right", "user", "_MTN/resolutions/file_3"), 1, nil, true)
check(samelines("stderr",
{"mtn: misuse: other resolution is content_user; specify 'drop', 'rename', or 'user_rename'"}))

-- specify right first
check(mtn("conflicts", "store", left_1, right_1), 0, nil, true)
-- resolve file_2
check(mtn("conflicts", "resolve_first_left", "keep"), 0, nil, nil) 
check(mtn("conflicts", "resolve_first_right", "drop"), 0, nil, nil)

-- file_3
check(mtn("conflicts", "resolve_first_right", "keep"), 0, nil, true)

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_3'",
 "mtn: dropped and recreated on the left",
 "mtn: modified on the right",
 "mtn: right_resolution: keep",
 "mtn: possible resolutions:",
 "mtn: resolve_first_left drop",
 "mtn: resolve_first_left rename",
 "mtn: resolve_first_left user_rename \"new_content_name\" \"new_file_name\""}))
check(mtn("conflicts", "resolve_first_left", "user", "_MTN/resolutions/file_3"), 1, nil, true)
check(samelines("stderr",
{"mtn: misuse: other resolution is keep; specify 'drop', 'rename', or 'user_rename'"}))

-- reset for case 4 reversed
check(mtn("conflicts", "store", left_1, right_1), 0, nil, true)
-- resolve file_2
check(mtn("conflicts", "resolve_first_left", "keep"), 0, nil, nil) 
check(mtn("conflicts", "resolve_first_right", "drop"), 0, nil, nil)

check(mtn("conflicts", "resolve_first_right", "user", "_MTN/resolutions/file_3"), 0, nil, nil)
check(mtn("conflicts", "resolve_first_left", "user", "_MTN/resolutions/file_3"), 1, nil, true)
check(samelines("stderr",
{"mtn: misuse: other resolution is content_user; specify 'drop', 'rename', or 'user_rename'"}))

-- Test error from user rename both sides to same new name. The error is at merge time.
check(mtn("conflicts", "store", left_1, right_1), 0, nil, true)
check(mtn("conflicts", "resolve_first_left", "rename", "file_2_renamed"), 0, nil, nil) 
check(mtn("conflicts", "resolve_first_right", "rename", "file_2_renamed"), 0, nil, nil) 
-- file_3
check(mtn("conflicts", "resolve_first_left", "drop"), 0, nil, nil)
check(mtn("conflicts", "resolve_first_right", "drop"), 0, nil, nil)
check(mtn("explicit_merge", "--resolve-conflicts", left_1, right_1, "testbranch"), 1, nil, true)
check(qgrep("'file_2_renamed' already exists", "stderr"))

-- end of file
