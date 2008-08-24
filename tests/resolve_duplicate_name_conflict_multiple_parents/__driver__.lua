-- Test/demonstrate handling of a duplicate name conflict, with
-- multiple parents of a suture.
--
-- We have this revision history graph:
--
--       A3  B4  C5  D6    A3  B4  C5  D6
--        \ /     \ /       \ /     \ /
--         E7     F8         G9     H10
--          \    /            \    /
--           \  /              \  /
--           I11                J12
--                  \       /
--                     K13
--
-- node numbers are the node ids used by monotone for checkout.sh in
-- each revision; root is 1, randomfile is 2.
--
-- In A, B, C, D, four developers each add a file 'checkout.sh'.
--
-- In E, F, G, H, each developer sutures two of them.
-- In I, J, two developers suture more.
-- In K, the final suture is done.

-- Then we explore some other merge situations.

function merged_revision()
  local workrev = readfile("stderr")
  local extract = string.gsub(workrev, "^.*mtn: %[merged%] (%x*).*$", "%1")
  if extract == workrev then
    err("failed to extract merged revision from stderr")
  end
  return extract
end

mtn_setup()

--  Get a non-empty base revision, for revert_to
addfile("randomfile", "blah blah blah")
commit("testbranch", "base")
base = base_revision()

addfile("checkout.sh", "A3")
commit("testbranch", "A3")
A3 = base_revision()

revert_to(base)

addfile("checkout.sh", "B4")
commit("testbranch", "B4")
B4 = base_revision()

revert_to(base)

addfile("checkout.sh", "C5")
commit("testbranch", "C5")
C5 = base_revision()

revert_to(base)

addfile("checkout.sh", "D6")
commit("testbranch", "D6")
D6 = base_revision()

-- First round of merges
writefile ("checkout.sh", "E7")
check(mtn("explicit_merge", "--resolve-conflicts", "resolved_suture \"checkout.sh\"", A3, B4, "testbranch"), 0, nil, true)
E7 = merged_revision()

writefile ("checkout.sh", "F8")
check(mtn("explicit_merge", "--resolve-conflicts", "resolved_suture \"checkout.sh\"", C5, D6, "testbranch"), 0, nil, true)
F8 = merged_revision()

writefile ("checkout.sh", "G9")
check(mtn("explicit_merge", "--resolve-conflicts", "resolved_suture \"checkout.sh\"", A3, B4, "testbranch"), 0, nil, true)
G9 = merged_revision()

writefile ("checkout.sh", "H10")
check(mtn("explicit_merge", "--resolve-conflicts", "resolved_suture \"checkout.sh\"", C5, D6, "testbranch"), 0, nil, true)
H10 = merged_revision()

-- Second round
writefile ("checkout.sh", "I11")
check(mtn("explicit_merge", "--resolve-conflicts", "resolved_suture \"checkout.sh\"", E7, F8, "testbranch"), 0, nil, true)
I11 = merged_revision()

writefile ("checkout.sh", "J12")
check(mtn("explicit_merge", "--resolve-conflicts", "resolved_suture \"checkout.sh\"", G9, H10, "testbranch"), 0, nil, true)
J12 = merged_revision()

-- Final merge
writefile ("checkout.sh", "K13")
check(mtn("explicit_merge", "--resolve-conflicts", "resolved_suture \"checkout.sh\"", I11, J12, "testbranch"), 0, nil, true)
K13 = merged_revision()

-- Merge I11 with G9 encounters a content conflict, not a suture
-- conflict; they share common parents
check(mtn("automate", "show_conflicts", I11, G9), 0, true, nil)
check(qgrep("conflict content", "stdout"))
-- FIXME: this should not be resolvable by the internal merger
-- check(not qgrep("resolved_internal", "stdout"))

writefile ("checkout.sh", "L14")
check(mtn("explicit_merge", "--resolve-conflicts", "resolved_user \"checkout.sh\"", I11, G9, "testbranch"), 0, nil, true)
L14 = merged_revision()

-- Similarly for L14 with H10
writefile ("checkout.sh", "M15")
check(mtn("explicit_merge", "--resolve-conflicts", "resolved_user \"checkout.sh\"", L14, H10, "testbranch"), 0, nil, true)

-- end of file
