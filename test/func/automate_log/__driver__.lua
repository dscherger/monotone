-- Test 'mtn automate log'

mtn_setup()

-- empty branch
check(mtn("automate", "log"), 0, false, true)
check(qgrep("warning: workspace has no parent revision,", "stderr"))

includecommon("automate_ancestry.lua")
includecommon("automate_stdio.lua")

revs = make_graph()
--   A
--  / \
-- B   C
--     |\
--     D E
--     \/
--      F
-- workspace is at F

-- Default 'from' is F
-- '--to' is not inclusive; '--from' is.
revmap("log", {"--to", revs.a}, {revs.f, revs.d, revs.e, revs.c}, false)
revmap("log", {"--to", revs.b, "--from", revs.b}, {revs.b}, false)
revmap("log", {"--to", revs.e}, {revs.f, revs.d}, false)
revmap("log", {"--to", revs.c, "--from", revs.d}, {revs.d}, false)

-- show missing revisions
revert_to(revs.c)
revmap("log", {"--to", "w:", "--from", "h:"}, {revs.b, revs.f, revs.d, revs.e}, false)

-- test automate stdio log
check(run_stdio("l3:loge", 0, 0, "m") == revs.c .. "\n" .. revs.a .. "\n")

-- end of file
