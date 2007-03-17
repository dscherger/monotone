
mtn_setup()

check(mtn("automate", "erase-ancestors", "c7539264e83c5d6af4c792f079b5d46e9c128665"), 1, false, false)

--   A
--  / \
-- B   C
--     |\
--     D E
--     \/
--      F
include("/common/automate_ancestry.lua")

revs = make_graph()

-- Now do some checks

revmap("erase-ancestors", {}, {})
for _,x in pairs(revs) do
  revmap("erase-ancestors", {x}, {x})
end
revmap("erase-ancestors", {revs.a, revs.b}, {revs.b})
revmap("erase-ancestors", {revs.a, revs.c}, {revs.c})
revmap("erase-ancestors", {revs.a, revs.f}, {revs.f})

revmap("erase-ancestors", {revs.b, revs.c}, {revs.b, revs.c})
revmap("erase-ancestors", {revs.a, revs.b, revs.c}, {revs.b, revs.c})
revmap("erase-ancestors", {revs.b, revs.f}, {revs.b, revs.f})
revmap("erase-ancestors", {revs.a, revs.b, revs.f}, {revs.b, revs.f})
