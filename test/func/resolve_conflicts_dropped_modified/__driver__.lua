-- Test reporting and resolving drop/modified conflicts

mtn_setup()

-- Create conflicts; modify and rename file in one head, drop in
-- other.
-- 
-- Six conflicts to test the three possible resolutions, with drop on
-- both left and right. Number in file name is the node number (helps
-- in debugging; node 1 is the root directory).

addfile("file_2", "file_2 base") -- modify/rename left, drop right; drop
addfile("file_3", "file_3 base") -- drop left, modify/rename right; drop
addfile("file_4", "file_4 base") -- modify left, drop right; keep
addfile("file_5", "file_5 base") -- drop left, modify right; keep
addfile("file_6", "file_6 base") -- modify/rename left, drop right; user
addfile("file_7", "file_7 base") -- drop left, modify/rename right; user
commit("testbranch", "base")
base = base_revision()

writefile("file_2", "file_2 left")
check(mtn("mv", "file_2", "file_2_renamed"), 0, false, false)

check(mtn("drop", "file_3"), 0, false, false)

writefile("file_4", "file_4 left")

check(mtn("drop", "file_5"), 0, false, false)

writefile("file_6", "file_6 left")
check(mtn("mv", "file_6", "file_6_renamed"), 0, false, false)

check(mtn("drop", "file_7"), 0, false, false)

commit("testbranch", "left 1")
left_1 = base_revision()

revert_to(base)

check(mtn("drop", "file_2"), 0, false, false)

writefile("file_3", "file_3 right")
check(mtn("mv", "file_3", "file_3_renamed"), 0, false, false)

check(mtn("drop", "file_4"), 0, false, false)

writefile("file_5", "file_5 right")

check(mtn("drop", "file_6"), 0, false, false)

writefile("file_7", "file_7 right")
check(mtn("mv", "file_7", "file_7_renamed"), 0, false, false)

commit("testbranch", "right 1")
right_1 = base_revision()

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
check(qgrep("replacing 'file_6_renamed' with '_MTN/resolutions/file_6_resolved", "stderr"))
check(qgrep("replacing 'file_7_renamed' with '_MTN/resolutions/file_7_resolved", "stderr"))

-- FIXME: add dropped_modified to 'show_conflicts' test (etc?)
-- better to put those tests here
                           
-- end of file
