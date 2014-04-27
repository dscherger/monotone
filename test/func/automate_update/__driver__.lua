-- Test 'automate update'
includecommon("automate_stdio.lua")

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

-- normal automate
check(mtn("automate", "update"), 0, false, true)
check(qgrep("mtn: updated to base revision", "stderr"))

revert_to(rev1)

-- automate stdio
progress = run_stdio("l6:updatee", 0, 0, 'p')
check(string.find(progress[#progress], "updated to base revision") ~= nil)

revert_to(rev1)
-- don't run external merger via automate stdio, since if it's opendiff it needs to prompt
-- automate stdio sets --non-interactive
writefile("patched", "a conflicting change")
progress = run_stdio("l6:updatee", 2, 0, 'e')
check(progress[#progress] == "misuse: can't spawn external merger when non-interactive")

-- Command error cases

-- no arguments allowed
check(mtn("automate", "update", "foo"), 1, false, true)
check(qgrep("wrong argument count", "stderr"))

-- only one revision selector
check(mtn("automate", "update", "-r 123", "-r 456"), 1, false, true)
check(qgrep("at most one revision selector may be specified", "stderr"))

-- other errors are handled by the same code as 'mtn update', so don't
-- need to be tested here.

-- end of file
