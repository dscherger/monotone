-- Test/demonstrate handling of inconsistent resolutions of a duplicate name conflict.
--
-- We have this revision history graph:
--
-- FIXME: add more files to show other resolution choices at each step
--
--                 o
--                / \
--              A1a  B2b
--              /| \ /|
--             / |  X |
--            /  C / \|
--           /   |/  D3d
--         E1e  F2b
--
-- 'o' is the base revision. In 'A' and 'B', Abe and Beth each add a
-- file with the same name 'checkout.sh'.
--
-- in 'D', Beth resolves the duplicate name conflict by suturing.
--
-- in 'C', Abe prepares to resolve the duplicate name conflict by
-- droppng his version; he merges in F, keeping Beth's (Abe hasn't
-- learned about suture yet).
--
-- in 'E', Jim edits checkout.sh.
--
-- Then we consider the two possible merge orders for D, E, and F, and
-- show that they produce consistent results.
--
-- Merging E, F to G encounters a content/drop conflict, resolved by suture.
-- Merging G, D to H encounters a content conflict, resolved by keeping Jim's content.
--
--                 o
--                / \
--              A1a  B2b
--              /| \ /|
--             / |  X |
--            /  C / \|
--           /   |/  D3d
--         E1e  F2b  /
--           \  /   /
--            G4e  /
--              \ /
--              H5e
--
-- Merging D, F to G first gives one drop/suture conflict, resolved by ignore_drop.
-- Merging E and G to H is then a content conflict, resolved by keeping
-- Jim's content:
--
--                 o
--                / \
--              A1a  B2b
--              /| \ /|
--             / |  X |
--            /  C / \|
--           /   |/  D3d
--         E1e  F2b  /
--           \    \ /
--            \   G3d
--             \  /
--              H3e

mtn_setup()

--  Get a non-empty base revision
addfile("randomfile", "blah blah blah")
commit()
base = base_revision()

-- Abe adds his file
addfile("checkout.sh", "checkout.sh abe 1")
commit("testbranch", "rev_A")
rev_A = base_revision()

revert_to(base)

-- Beth adds her file and merges to D via suture
addfile("checkout.sh", "checkout.sh beth 1")
commit("testbranch", "rev_B")
rev_B = base_revision()

-- Beth sutures
check(mtn("automate", "show_conflicts"), 0, true, nil)
writefile ("checkout.sh", "checkout.sh merged")
get ("merge_a_b-conflicts-resolve", "_MTN/conflicts")
check(mtn("merge", "--resolve-conflicts-file=_MTN/conflicts"), 0, nil, false)
check(mtn("update"), 0, nil, false)
rev_D = base_revision()

-- Abe merges to F via drop
revert_to(rev_A)

check(mtn("drop", "checkout.sh"), 0, nil, false)
commit("testbranch", "rev_C")
rev_C = base_revision()

check(mtn("explicit_merge", rev_C, rev_B, "testbranch"), 0, nil, false)
check(mtn("update"), 0, nil, false)
check("checkout.sh beth 1" == readfile("checkout.sh"))
rev_F = base_revision()

-- Jim edits to get E
revert_to(rev_A)

writefile("checkout.sh", "checkout.sh jim 1")

commit("testbranch", "rev_E")
rev_E = base_revision()

-- plain 'merge' chooses to merge E, F first; that gives duplicate name and content/drop conflicts
-- which just shows that 'drop' is _not_ a good way to resolve duplicate name conflicts.
check(mtn("merge"), 1, nil, true)
canonicalize("stderr")
check(samefilestd("merge_e_f-message-1", "stderr"))

check(mtn("automate", "show_conflicts"), 0, true, nil)
canonicalize("stdout")
check(samefilestd("merge_e_f-conflicts", "stdout"))

-- first resolution attempt fails
get("merge_e_f-conflicts-resolve-1", "_MTN/conflicts")
check(mtn("merge", "--resolve-conflicts-file=_MTN/conflicts"), 1, nil, true)
canonicalize("stderr")
check(samefilestd("merge_e_f-message-2", "stderr"))

-- second resolution attempt succeeds
get("merge_e_f-conflicts-resolve-2", "_MTN/conflicts")
check(mtn("merge", "--resolve-conflicts-file=_MTN/conflicts"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("merge_e_f-message-3", "stderr"))

-- Now merge G, D
-- This fails with duplicate name conflict
check(mtn("merge"), 1, nil, true)
canonicalize("stderr")
check(samefilestd("merge_g_d-message-1", "stderr"))

check(mtn("merge", "--resolve-conflicts", 'resolved_suture "checkout.sh"'), 0, nil, true)
canonicalize("stderr")
check(samefilestd("merge_g_d-message-2", "stderr"))

check(mtn("update"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("update-message-1", "stderr"))
rev_H = base_revision()

-- go back and try other merge order
revert_to(rev_E)
check(mtn("db", "kill_rev_locally", rev_H), 0, nil, false)
check(mtn("db", "kill_rev_locally", "98997255d9ff7355d9e5ee2287aba2e8e8fe33e0"), 0) -- rev_G

-- This gives a drop/suture conflict
check(mtn("explicit_merge", rev_D, rev_F, "testbranch"), 1, nil, true)
check(mtn("update", "--revision=59d830bd65520e2a961aae0d31afd9bd24799b5e"), 0, nil, false)
-- end of file
