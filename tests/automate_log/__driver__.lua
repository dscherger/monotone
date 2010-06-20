-- Test 'mtn automate log'

mtn_setup()

-- empty branch
check(mtn("automate", "log"), 1, false, true)
check(qgrep("misuse: workspace parent revision '' not found", "stderr"))

include("/common/automate_ancestry.lua")

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

-- end of file
