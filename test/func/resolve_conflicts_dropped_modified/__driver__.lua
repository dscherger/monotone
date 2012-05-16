-- Test reporting and resolving drop/modified conflicts
--
-- this is currently not supported; we are documenting a test case
-- that must be considered when implementing it.

mtn_setup()

-- Create conflict; modify and rename file in one head, drop in other
addfile("foo", "foo base")
commit("testbranch", "base")
base = base_revision()

writefile("foo", "foo left")
check(mtn("mv", "foo", "bar"), 0, false, false)

commit("testbranch", "left 1")
left_1 = base_revision()

revert_to(base)

writefile("foo", "foo right")
commit("testbranch", "right 1")
right_1 = base_revision()

check(mtn("drop", "foo"), 0, false, false)
commit("testbranch", "right 2")
right_2 = base_revision()

check(mtn("conflicts", "store"), 0, nil, true)
check(samelines("stderr",
{"mtn: 1 conflict with supported resolutions.",
 "mtn: stored in '_MTN/conflicts'"}))

canonicalize("_MTN/conflicts")
check(samefilestd("conflicts-1", "_MTN/conflicts"))

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
{"mtn: conflict: file 'bar'",
 "mtn: dropped on the left",
 "mtn: modified on the right",
 "mtn: possible resolutions:",
 "mtn: resolve_first drop",
 "mtn: resolve_first keep",
 "mtn: resolve_first user \"name\""}))

check(mtn("merge", "--resolve-conflicts"), 1, nil, true)
check(qgrep("no resolution provided for", "stderr"))
             
check(mtn("conflicts", "resolve_first", "drop"), 0, nil, true)

canonicalize("_MTN/conflicts")
check(samefilestd("conflicts-1-resolved", "_MTN/conflicts"))

check(mtn("merge", "--resolve-conflicts"), 0, nil, true)
check(qgrep("dropping 'bar'", "stderr"))

-- FIXME: do three files at once: swap left/right, resolve keep; resolve user

-- FIXME: add dropped_modified to 'show_conflicts' test (etc?)
                           
-- end of file
