-- Test 'automate update'
include("/common/automate_stdio.lua")

mtn_setup()

----------
-- create a basic file history; add some files, then operate on
-- each of them in some way.

addfile("dropped", "dropped")
addfile("original", "original")
addfile("unchanged", "unchanged")
addfile("patched", "patched")
commit()
rev1 = base_revision()

addfile("added", "added")

writefile("patched", "something has changed")

check(mtn("rename", "original", "renamed"), 0, false, false)
check(mtn("drop", "dropped"), 0, false, false)
commit()
rev2 = base_revision()

revert_to(rev1)

check(mtn("automate", "update"), 0, false, true)
check(qgrep("mtn: updated to base revision", "stderr"))

revert_to(rev1)

progress = run_stdio("l6:updatee", 0, 0, 'p')
check(string.find(progress[9], "updated to base revision") ~= nil)

-- Command error cases

-- no arguments allowed
run_stdio("l6:update3:fooe", 2)

-- end of file
