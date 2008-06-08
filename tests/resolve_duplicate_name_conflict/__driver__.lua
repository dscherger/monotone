-- Test/demonstrate handling of a duplicate name conflict.
--
-- In the first step, we have this revision history graph:
--
--                   o
--                  / \
--                 a   b
--                  \ /
--                   d
--
-- 'o' is the base revision. In 'a' and 'b', Abe and Beth each add two
-- files with the same names:
--
--      checkout.sh
--          the user intent is that there be one file with that name;
--          the contents should be merged.
--
--      thermostat.c
--          the user intent is that there should be two files;
--          thermostat-westinghouse.c and thermostat-honeywell.c
--
-- in 'd', the duplicate name conflicts are resolved by suturing
-- checkout.sh and renaming thermostat.c.
--
-- After that, we extend the history graph; see below.

mtn_setup()
include ("common/test_utils_inventory.lua")

--  Get a non-empty base revision
addfile("randomfile", "blah blah blah")
commit()
base = base_revision()

-- Abe adds conflict files
addfile("thermostat.c", "thermostat westinghouse abe 1")
addfile("checkout.sh", "checkout.sh abe 1")
commit("testbranch", "abe_1")
abe_1 = base_revision()

revert_to(base)

-- Beth adds files, and attempts to merge
addfile("thermostat.c", "thermostat honeywell beth 1")
addfile("checkout.sh", "checkout.sh beth 1")
commit("testbranch", "beth_1")
beth_1 = base_revision()

-- This fails due to duplicate name conflicts
check(mtn("merge"), 1, nil, false)

-- Beth uses 'automate show_conflicts' and 'merge --resolve-conflicts-file'
-- to fix the conflicts
--
-- For thermostat.c, she renames the files in her workspace (to allow
-- testing), and records the resolution as 'resolved_rename_left' and
-- 'resolved_rename_right'
--
-- For checkout.sh, she retrieves Abe's version to merge with hers
-- (leaving the result in her workspace), using 'automate
-- get_file_of'. This requires knowing the revision id of Abe's
-- commit, which we get from 'automate show_conflicts'. In
-- _MTN/conflicts, she records the resolution as
-- 'resolved_content_ws'.

check (mtn("automate", "show_conflicts"), 0, true, nil)

-- Verify that we got the expected revisions, conflicts and file ids
parsed = parse_basic_io(readfile("stdout"))

-- The Lua parser returns the 'conflict <symbol>' line as two lines
-- with no values, so the line count here seems odd.
check_basic_io_line (1, parsed[1], "left", abe_1)
check_basic_io_line (2, parsed[2], "right", beth_1)
check_basic_io_line (3, parsed[3], "ancestor", base)

check_basic_io_line (4, parsed[4], "conflict")
check_basic_io_line (5, parsed[5], "duplicate_name")
check_basic_io_line (6, parsed[6], "left_type", "added file")
check_basic_io_line (7, parsed[7], "left_name", "checkout.sh")
check_basic_io_line (8, parsed[8], "left_file_id", "61b8d4fb0e5d78be111f691b955d523c782fa92e")
abe_checkout = parsed[8].values[1]

check_basic_io_line (15, parsed[15], "left_name", "thermostat.c")
check_basic_io_line (16, parsed[16], "left_file_id", "c2f67aa3b29c2bdab4790213c7f3bf73e58440a7")
abe_thermostat = parsed[16].values[1]

-- Do the filesystem rename for Beth's thermostat.c, and retrieve Abe's version
rename ("thermostat.c", "thermostat-honeywell.c")

check (mtn ("automate", "get_file", abe_thermostat), 0, true, nil)
rename ("stdout", "thermostat-westinghouse.c")
check ("thermostat westinghouse abe 1" == readfile ("thermostat-westinghouse.c"))

-- Do the manual merge for checkout.sh; retrieve Abe's version
check (mtn ("automate", "get_file", abe_checkout), 0, true, nil)
rename ("stdout", "checkout.sh-abe")
check ("checkout.sh abe 1" == readfile ("checkout.sh-abe"))

get ("checkout.sh-merged", "checkout.sh")

-- This has the resolution lines
get ("conflicts-resolved", "_MTN/conflicts")

-- This succeeds
check(mtn("merge", "--resolve-conflicts-file=_MTN/conflicts"), 0, nil, true)
canonicalize("stderr")
get ("expected-merge-messages-abe_1-beth_1")
check(samefile("expected-merge-messages-abe_1-beth_1", "stderr"))

-- update fails if thermostat.c is missing, and if
-- thermostat-honeywell.c, thermostat-westinghouse.c are in the way.
-- So clean that up first. FIXME: update needs --ignore-missing,
-- --overwrite-ws or something.
check(mtn("revert", "--missing"), 0, nil, false)
remove ("thermostat-honeywell.c")
remove ("thermostat-westinghouse.c")
check(mtn("update"), 0, nil, true)
-- FIXME: this warns about changes in checkout.sh being lost, even though they aren't; it doesn't understand suture
canonicalize("stderr")
get ("expected-update-messages-jim_1")
check(samefile("expected-update-messages-jim_1", "stderr"))

-- verify that we got revision_format 2
check(mtn("automate", "get_revision", base_revision()), 0, true, nil)
canonicalize("stdout")
get ("expected-merged-revision-jim_1")
check(samefile("expected-merged-revision-jim_1", "stdout"))

-- Verify file contents
check("thermostat westinghouse abe 1" == readfile("thermostat-westinghouse.c"))
check("thermostat honeywell beth 1" == readfile("thermostat-honeywell.c"))
check("checkout.sh merged\n" == readfile("checkout.sh"))

-- In the second step, we extend the revision history graph:
--
--                   o
--                  / \
--                 a   b
--                / \ / \
--               c   d   e
--                \ / \ /
--                 g   h
--                  \ /
--                   f
--
-- Here we assume that the 'd' merge was done by Jim, and Abe and Beth
-- edit their new files in parallel with the merge. We pretend Abe,
-- Beth, and Jim have separate development databases, shared via
-- netsync. Eventually everything is merged properly.
--
-- in 'c' and 'e', Abe and Beth edit checkout.sh and thermostat.c
--
-- in 'g' and 'h', Abe and Beth each merge their changes with Jim's merge.
--
-- in 'f', Jim merges one more time.

jim_1 = base_revision()

-- Abe edits his files and merges
revert_to(abe_1)

writefile("thermostat.c", "thermostat westinghouse abe 2")
writefile("checkout.sh", "checkout.sh abe 2")
commit("testbranch", "abe_2")
abe_2 = base_revision()

check(mtn("merge"), 0, nil, true)
canonicalize("stderr")
get ("expected-merge-messages-abe_2-jim_1")
check(samefile("expected-merge-messages-abe_2-jim_1", "stderr"))

-- Beth edits her files and merges
revert_to(beth_1)

writefile("thermostat.c", "thermostat honeywell beth 1")
writefile("checkout.sh", "checkout.sh beth 1")
commit("testbranch", "beth_2")
beth_2 = base_revision()

-- If we just do 'merge', mtn will merge 'e' and 'g', since those are
-- the current heads. To emulate separate development databases, we
-- specify the revisions to merge.
check(mtn("merge", jim_1, beth_2), 0, nil, true)
canonicalize("stderr")
get ("expected-merge-messages-jim_1-beth_2")
check(samefile("expected-merge-messages-jim_1-beth_2", "stderr"))

-- end of file
