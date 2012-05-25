-- Test reporting and resolving drop/modified conflicts

mtn_setup()

-- Create conflicts; modify and rename file in one head, drop in
-- other.
-- 
-- Three conflicts to test the three possible resolutions, and
-- left/right swap.

addfile("foo", "foo base")
addfile("file_2", "file_2 base")
addfile("file_3", "file_3 base")
commit("testbranch", "base")
base = base_revision()

writefile("foo", "foo left")
check(mtn("mv", "foo", "bar"), 0, false, false)

writefile("file_2", "file_2 left")
writefile("file_3", "file_3 left")

commit("testbranch", "left 1")
left_1 = base_revision()

check(mtn("drop", "file_3"), 0, false, false)
commit("testbranch", "left 2")
left_2 = base_revision()

revert_to(base)

writefile("foo", "foo right")
writefile("file_2", "file_2 right")
writefile("file_3", "file_3 right")

commit("testbranch", "right 1")
right_1 = base_revision()

check(mtn("drop", "foo"), 0, false, false)
check(mtn("drop", "file_2"), 0, false, false)
commit("testbranch", "right 2")
right_2 = base_revision()


check(mtn("conflicts", "store", left_2, right_2), 0, nil, true)
check(samelines("stderr",
{"mtn: 3 conflicts with supported resolutions.",
 "mtn: stored in '_MTN/conflicts'"}))

canonicalize("_MTN/conflicts")
check(samefilestd("conflicts", "_MTN/conflicts"))

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_2'",
 "mtn: modified on the left",
 "mtn: dropped on the right",
 "mtn: possible resolutions:",
 "mtn: resolve_first drop",
 "mtn: resolve_first keep",
 "mtn: resolve_first user \"name\""}))

check(mtn("conflicts", "resolve_first", "keep"), 0, nil, true)

-- check for nice error message if not all conflicts are resolved
check(mtn("merge", "--resolve-conflicts"), 1, nil, true)
check(qgrep("no resolution provided for", "stderr"))
             
check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'file_3'",
 "mtn: dropped on the left",
 "mtn: modified on the right",
 "mtn: possible resolutions:",
 "mtn: resolve_first drop",
 "mtn: resolve_first keep",
 "mtn: resolve_first user \"name\""}))

mkdir("_MTN/resolutions")
writefile("_MTN/resolutions/file_3_resolved", "file_3 resolved")
check(mtn("conflicts", "resolve_first", "user", "file_3_resolved"), 0, nil, true)

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'bar'",
 "mtn: modified on the left",
 "mtn: dropped on the right",
 "mtn: possible resolutions:",
 "mtn: resolve_first drop",
 "mtn: resolve_first keep",
 "mtn: resolve_first user \"name\""}))

check(mtn("conflicts", "resolve_first", "drop"), 0, nil, true)

canonicalize("_MTN/conflicts")
check(samefilestd("conflicts-resolved", "_MTN/conflicts"))
check(mtn("merge", "--resolve-conflicts"), 0, nil, true)
check(qgrep("dropping 'bar'", "stderr"))
check(qgrep("keeping 'file_2'", "stderr"))
check(qgrep("replacing 'file_3' with '_MTN/resolutions/file_3_resolved", "stderr"))

-- FIXME: add dropped_modified to 'show_conflicts' test (etc?)
                           
-- end of file
