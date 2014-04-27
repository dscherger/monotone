
mtn_setup()

-- Make sure we fail when given a non-existent revision
check(mtn("automate", "erase_descendants", "c7539264e83c5d6af4c792f079b5d46e9c128665"), 1, false, false)

--   A
--  / \
-- B   C
--     |\
--     D E
--     \/
--      F
includecommon("automate_ancestry.lua")

revs = make_graph()

-- Now do some checks

-- Empty input gives empty output
revmap("erase_descendants", {}, {})

-- Single revision input gives the same single revision output
for _,x in pairs(revs) do
  revmap("erase_descendants", {x}, {x})
end

-- Whole revision graph should give roots - A in this case
revmap("erase_descendants", {revs.a, revs.b, revs.c, revs.d, revs.e, revs.f}, {revs.a})

-- Sibling only inputs should give the same output
revmap("erase_descendants", {revs.b, revs.c}, {revs.b, revs.c})
revmap("erase_descendants", {revs.d, revs.e}, {revs.d, revs.e})

-- Siblings with descendants should give just the siblings
revmap("erase_descendants", {revs.b, revs.c, revs.d, revs.e, revs.f}, {revs.b, revs.c})
revmap("erase_descendants", {revs.d, revs.e, revs.f}, {revs.d, revs.e})

-- Leaves only input should give the same output
revmap("erase_descendants", {revs.b, revs.f}, {revs.b, revs.f})

-- Revision with its descendants should give just the revision
revmap("erase_descendants", {revs.c, revs.d, revs.e, revs.f}, {revs.c})
revmap("erase_descendants", {revs.e, revs.f}, {revs.e})


