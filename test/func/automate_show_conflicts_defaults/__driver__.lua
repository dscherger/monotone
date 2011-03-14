-- Test 'automate show_conflicts' argument defaults
--
-- See automate_show_conflicts for all conflict cases

includecommon("basic_io.lua")
includecommon("automate_stdio.lua")

mtn_setup()

--  Get a non-empty base revision, then create three heads
addfile("randomfile", "blah blah blah")
commit("testbranch", "base_1")
base_1 = base_revision()

-- Abe adds conflict files
addfile("checkout.sh", "checkout.sh abe 1")
addfile("thermostat.c", "thermostat westinghouse")
commit("testbranch", "abe_1")
abe_1 = base_revision()

revert_to(base_1)

-- Beth adds non-conflict files
addfile("user_guide.text", "really cool stuff")
commit("testbranch", "base_2")
base_2 = base_revision()

-- Beth adds conflict files
addfile("checkout.sh", "checkout.sh beth 1")
addfile("thermostat.c", "thermostat honeywell beth")
commit("testbranch", "beth_1")
beth_1 = base_revision()

revert_to(base_2)

-- Chuck adds conflict files
addfile("checkout.sh", "checkout.sh beth 1")
addfile("thermostat.c", "thermostat honeywell chuck")
commit("testbranch", "chuck_1")
chuck_1 = base_revision()

--  Check that 'automate show_conflicts' picks the same heads to
--  compare first as 'merge' does. Use workspace options
check(mtn("merge"), 1, false, false)
-- mtn: [left]  19ab79c40805c9dc5e25d0b6fa5134291e0b42d9 = beth_1
-- mtn: [right] d8a8bc9623c1ff9c0a5c082e40f0ff8ec6b43e72 = chuck_1

check(mtn_ws_opts("automate", "show_conflicts"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))

check_basic_io_line (1, parsed[1], "left", beth_1)
check_basic_io_line (2, parsed[2], "right", chuck_1)
check_basic_io_line (3, parsed[3], "ancestor", base_2)

-- In mtn 0.40 and earlier, this was broken, because automate stdio
-- did not re-read the workspace options for each command, so the
-- branch was null.
local showconfout = run_stdio("l14:show_conflictse", 0)
parsed = parse_basic_io(showconfout)
check_basic_io_line (1, parsed[1], "left", beth_1)
check_basic_io_line (2, parsed[2], "right", chuck_1)
check_basic_io_line (3, parsed[3], "ancestor", base_2)

--  Check that 'automate show_conflicts' works outside workspace when options are specified
non_ws_dir = make_temp_dir()
check(indir(non_ws_dir, mtn_outside_ws("automate", "show_conflicts", "--branch=testbranch")), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

check_basic_io_line (1, parsed[1], "left", beth_1)
check_basic_io_line (2, parsed[2], "right", chuck_1)
check_basic_io_line (3, parsed[3], "ancestor", base_2)

--  Check error message when outside workspace when branch not specified
check(indir(non_ws_dir, mtn_outside_ws("automate", "show_conflicts")), 1, false, true)
check(qgrep("misuse: please specify a branch, with '--branch=BRANCH'", "stderr"))

--  Check error message for wrong arg count
check(indir(non_ws_dir, mtn_outside_ws("automate", "show_conflicts", "rev1")), 1, false, true)
check(qgrep("wrong argument count", "stderr"))

-- end of file
