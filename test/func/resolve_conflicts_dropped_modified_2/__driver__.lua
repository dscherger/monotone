-- Verify that we can resolve this extended use case involving a
-- dropped_modified conflict:
-- 
--     A
--    / \
--   M1  D
--   | \ |
--   M2  P
--    \ /
--     Q
--
-- The file is modified and merged into the dropped branch twice.

mtn_setup()

addfile("file_2", "file_2 base") -- modify/rename left, drop right; drop
commit("testbranch", "base")
base = base_revision()

writefile("file_2", "file_2 left 1")

commit("testbranch", "left 1")
left_1 = base_revision()

revert_to(base)

check(mtn("drop", "file_2"), 0, false, false)

commit("testbranch", "right 1")
right_1 = base_revision()

check(mtn("show_conflicts", left_1, right_1), 0, nil, true)
check(samelines("stderr",
 {"mtn: [left]     506d8ed51b06c0080e8bb307155a88637045b532",
  "mtn: [right]    a2889488ed1801a904d0219ec9939dfc2e9be033",
  "mtn: [ancestor] f80ff103551d0313647d6c84990bc9db6b158dac",
  "mtn: conflict: file 'file_2' from revision f80ff103551d0313647d6c84990bc9db6b158dac",
  "mtn: modified on the left, named file_2",
  "mtn: dropped on the right",
  "mtn: 1 conflict with supported resolutions."}))

check(mtn("conflicts", "store", left_1, right_1), 0, nil, true)

check(mtn("conflicts", "resolve_first", "keep"), 0, nil, true)

check(mtn("explicit_merge", "--resolve-conflicts", left_1, right_1, "testbranch"), 0, nil, true)
check(samelines("stderr",
 {"mtn: [left]  506d8ed51b06c0080e8bb307155a88637045b532",
  "mtn: [right] a2889488ed1801a904d0219ec9939dfc2e9be033",
  "mtn: keeping 'file_2' from left",
  "mtn: history for 'file_2' from left will be lost; see user manual Merge Conflicts section",
  "mtn: [merged] 3df3126220588440def7b08f488ca35eaa94f1b6"}))

check(mtn("update"), 0, nil, true)
check(samelines("file_2", {"file_2 left 1"}))

right_2 = base_revision()

-- round 2; modify the file again
revert_to(left_1)

writefile("file_2", "file_2 left 2")

commit("testbranch", "left 2")
left_2 = base_revision()

check(mtn("show_conflicts", left_2, right_2), 0, nil, true)
check(samelines("stderr",
 {"mtn: [left]     5a144a43f03692e389f3ddd4c510a4d9754061d5",
  "mtn: [right]    3df3126220588440def7b08f488ca35eaa94f1b6",
  "mtn: [ancestor] 506d8ed51b06c0080e8bb307155a88637045b532",
  "mtn: conflict: file 'file_2' from revision 506d8ed51b06c0080e8bb307155a88637045b532",
  "mtn: modified on the left, named file_2",
  "mtn: dropped and recreated on the right",
  "mtn: 1 conflict with supported resolutions."}))

check(mtn("conflicts", "store", left_2, right_2), 0, nil, true)

check(mtn("conflicts", "resolve_first_left", "keep"), 0, nil, true)
check(mtn("conflicts", "resolve_first_right", "drop"), 0, nil, true)

check(mtn("explicit_merge", "--resolve-conflicts", left_2, right_2, "testbranch"), 0, nil, true)
check(qgrep("mtn: keeping 'file_2' from left", "stderr"))
check(qgrep("mtn: dropping 'file_2' from right", "stderr"))

check(mtn("update"), 0, nil, true)
check(samelines("file_2", {"file_2 left 2"}))

-- end of file
