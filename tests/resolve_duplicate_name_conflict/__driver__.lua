-- Test/demonstrate handling of a duplicate name conflict; Abe and
-- Beth add files with the same names.
--
-- For checkout.sh, the user intent is that there be
-- one file with that name; the contents should be merged.
--
-- For thermostat.c, there should be two files;
-- thermostat-westinghouse.c and thermostat-honeywell.c

mtn_setup()
include ("common/test_utils_inventory.lua")

--  Get a non-empty base revision
addfile("randomfile", "blah blah blah")
commit()
base = base_revision()

-- Abe adds conflict files
addfile("thermostat.c", "thermostat westinghouse")
addfile("checkout.sh", "checkout.sh abe 1")
commit("testbranch", "abe_1")
abe_1 = base_revision()

revert_to(base)

-- Beth adds files, and attempts to merge
addfile("thermostat.c", "thermostat honeywell")
addfile("checkout.sh", "checkout.sh beth 1")
commit("testbranch", "beth_1")
beth_1 = base_revision()

-- This fails due to duplicate name conflicts
check(mtn("merge"), 1, false, false)

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

check (mtn("automate", "show_conflicts"), 0, true, false)

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
check_basic_io_line (16, parsed[16], "left_file_id", "4cdcec6fa2f9d5c075d5b80d03c708c8e4801196")
abe_thermostat = parsed[16].values[1]

-- Do the filesystem rename for Beth's thermostat.c, and retrieve Abe's version
rename ("thermostat.c", "thermostat-honeywell.c")

check (mtn ("automate", "get_file", abe_thermostat), 0, true, false)
rename ("stdout", "thermostat-westinghouse.c")
check ("thermostat westinghouse" == readfile ("thermostat-westinghouse.c"))

-- Do the manual merge for checkout.sh; retrieve Abe's version
check (mtn ("automate", "get_file", abe_checkout), 0, true, false)
rename ("stdout", "checkout.sh-abe")
check ("checkout.sh abe 1" == readfile ("checkout.sh-abe"))

get ("checkout.sh-merged", "checkout.sh")

-- This has the resolution lines
get ("conflicts-resolved", "_MTN/conflicts")

-- This succeeds
check(mtn("merge", "--resolve-conflicts-file=_MTN/conflicts"), 0, true, true)

-- update fails if thermostat.c is missing, and if
-- thermostat-honeywell.c, thermostat-westinghouse.c are in the way.
-- So clean that up first. FIXME: update needs --ignore-missing,
-- --overwrite-ws or something.
check(mtn("revert", "--missing"), 0, false, false)
remove ("thermostat-honeywell.c")
remove ("thermostat-westinghouse.c")
check(mtn("update"), 0, true, true)

-- Verify file contents
check("thermostat westinghouse" == readfile("thermostat-westinghouse.c"))
check("thermostat honeywell" == readfile("thermostat-honeywell.c"))

-- This currently fails; the merge during update first adds then drops
-- checkout.sh. Need to change diediedie.
check("checkout.sh merged" == readfile("checkout.sh"))
-- end of file
