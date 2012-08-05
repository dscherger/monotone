-- diff should not compute diffs for files with mtn:manual_merge attributes
-- 
-- In 1.0 and earlier, it only ignored files that file_io.cc
-- guess_binary considered to be binary.
--
-- Consider a newly added 50 MB text file (Earth gravity
-- coefficients); this simply hangs the diff engine (it may return
-- eventually), so the user marks it with mtn:manual_merge.

mtn_setup()

addfile("small_file.text", "small file\n")
commit("base", "testbranch")
base = base_revision()

addfile("grav.coef", "50 MB of coefficients\n")
check(mtn("attr", "set", "grav.coef", "mtn:manual_merge", "true"), 0, nil, false)
commit("abe_1", "testbranch")
rev_1 = base_revision()

check(mtn("diff", "-r", base, "-r", rev_1), 0, true, nil)
check(not qgrep("^\\+\\+\\+ grav.coef", "stdout"))
check(qgrep("^# grav.coef is binary", "stdout"))
