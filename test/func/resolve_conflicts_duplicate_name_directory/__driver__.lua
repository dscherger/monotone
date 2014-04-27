-- Test/demonstrate handling of duplicate name directory conflicts.
-- 
-- For a directory, we can't merge the contents; we can only
-- drop (if empty) or rename.

mtn_setup()

--  Get a non-empty base revision
addfile("randomfile", "blah blah blah")
commit()
base = base_revision()

-- Abe adds conflict directory
mkdir("gui")
addfile("gui/gui.h", "gui.h abe")
commit("testbranch", "abe_1")
abe_1 = base_revision()

revert_to(base)

-- Beth adds directory
mkdir("gui")
addfile("gui/gui.h", "gui.h beth")
commit("testbranch", "beth_1")
beth_1 = base_revision()

-- Resolve conflict
check(mtn("conflicts", "store"), 0, false, true)
check(samelines("stderr", {"mtn: 1 conflict with supported resolutions.",
                           "mtn: stored in '_MTN/conflicts'"}))

check(mtn("conflicts", "show_first"), 0, nil, true)
check(qgrep("duplicate_name gui", "stderr"))

-- Attempt to drop one side; fails at merge time, due to non-empty directory.
check(mtn("conflicts", "resolve_first_left", "drop"), 0, nil, nil)
check(mtn("conflicts", "resolve_first_right", "rename", "gui"), 0, nil, nil)

check(mtn("merge", "--resolve-conflicts"), 1, nil, true)
check(qgrep("misuse: can't drop 'gui'; not empty", "stderr"))

-- Start again, use renames for both sides
check(mtn("conflicts", "clean"), 0, nil, true)

check(mtn("conflicts", "store"), 0, nil, true)
check(samelines("stderr", {"mtn: 1 conflict with supported resolutions.",
                           "mtn: stored in '_MTN/conflicts'"}))

check(mtn("conflicts", "show_first"), 0, nil, true)
check(qgrep("duplicate_name gui", "stderr"))

check(mtn("conflicts", "resolve_first_left", "rename", "gui_abe"), 0, nil, nil)
check(mtn("conflicts", "resolve_first_right", "rename", "gui_beth"), 0, nil, nil)

check(mtn("merge", "--resolve-conflicts"), 0, nil, true)

check(mtn("update"), 0, nil, false)
merged = base_revision()

check("gui.h beth" == readfile("gui_beth/gui.h"))

-- end of file
