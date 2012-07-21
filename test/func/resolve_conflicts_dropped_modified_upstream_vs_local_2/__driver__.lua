-- Show a problematic use case involving a dropped_modified conflict,
-- and how it can be resolved with the 'mtn:resolve_conflict'
-- attribute.
--
-- There is an upstream branch, and a local branch. The local branch
-- adds a file that the upstream branch adopts. The next merge from
-- upstream to local encounters a duplicate_name conflict; if we
-- resolve that the wrong way (keep local instead of upstream), on the
-- next merge we get a dropped_modified conflict.
--
-- In the meantime, we've edited the file locally, illustrating that
-- the dropped_modified conflict code needs to search thru history for
-- the rev last containing the dropped node id (this was a bug in an
-- earlier implementation).

mtn_setup()

addfile("file_1", "file_1 base")
commit("testbranch", "base")
base = base_revision()

writefile("file_1", "file_1 upstream 1")

commit("testbranch", "upstream 1")
upstream_1 = base_revision()

revert_to(base)

addfile("file_2", "file_2 local")

commit("testbranch", "local 1")
local_1 = base_revision()

revert_to(upstream_1)

addfile("file_2", "file_2 upstream 1")

commit("testbranch", "upstream 2")
upstream_2 = base_revision()

check(mtn("show_conflicts", upstream_2, local_1), 0, nil, true)
check(samelines("stderr",
 {"mtn: [left]     27d41ae9f2b3cb73b130d9845d77574a11021b17",
  "mtn: [right]    3cae692a68fa6710b1db5e73e3e876994c175925",
  "mtn: [ancestor] 736498437fa91538540dc5fbad750cbc1472d793",
  "mtn: conflict: duplicate name 'file_2' for the directory ''",
  "mtn: added as a new file on the left",
  "mtn: added as a new file on the right",
  "mtn: 1 conflict with supported resolutions."}))

check(mtn("conflicts", "store", upstream_2, local_1), 0, nil, true)

-- We should keep upstream, and drop local, but we get it backwards
check(mtn("conflicts", "resolve_first_left", "drop"), 0, nil, true)
check(mtn("conflicts", "resolve_first_right", "keep"), 0, nil, true)

check(mtn("explicit_merge", "--resolve-conflicts", upstream_2, local_1, "testbranch"), 0, nil, true)
check(samelines("stderr",
 {"mtn: [left]  27d41ae9f2b3cb73b130d9845d77574a11021b17",
  "mtn: [right] 3cae692a68fa6710b1db5e73e3e876994c175925",
  "mtn: dropping 'file_2'",
  "mtn: keeping 'file_2'",
  "mtn: [merged] fce2fe3c327a294209ad695e0658275f104fe12a"}))

check(mtn("update"), 0, nil, true)

-- One more local mod
writefile("file_2", "file_2 local 2")
commit("testbranch", "local 2")
local_2 = base_revision()

-- round 2; upstream modifies the file again, and we try to merge
revert_to(upstream_2)

writefile("file_2", "file_2 upstream 2")

commit("testbranch", "upstream 3")
upstream_3 = base_revision()

check(mtn("show_conflicts", upstream_3, local_2), 0, nil, true)
check(samelines("stderr",
 {"mtn: [left]     48b18ebc7b70733133539384e49a2eedb82e32b2",
  "mtn: [right]    650057e8a81bd41991dc5ff10b2d60343f1032ae",
  "mtn: [ancestor] 27d41ae9f2b3cb73b130d9845d77574a11021b17",
  "mtn: conflict: file 'file_2' from revision 27d41ae9f2b3cb73b130d9845d77574a11021b17",
  "mtn: modified on the left, named file_2",
  "mtn: dropped and recreated on the right",
  "mtn: 1 conflict with supported resolutions."}))

--  There are two nodes with filename 'file_2'; node 4 in upstream,
--  node 3 in local. At this point, node 4 is modified in upstream and
--  dropped in local; node 3 is unborn in upstream and modified in
--  local. Therefore this is a combination of dropped_modified and
--  duplicate_name conflicts, which we handle as a dropped_modified
--  conflict.
check(mtn("conflicts", "store", upstream_3, local_2), 0, nil, true)
check(samefilestd("conflicts_3_2", "_MTN/conflicts"))

-- since we have a duplicate name conflict, we need to specify both
-- right and left resolutions, so 'resolve_first' is wrong here
check(mtn("conflicts", "resolve_first", "keep"), 1, nil, true)
check(qgrep("must specify 'resolve_first_left' or 'resolve_first_right'", "stderr"))

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
 {"mtn: conflict: file 'file_2'",
  "mtn: modified on the left",
  "mtn: dropped and recreated on the right",
  "mtn: possible resolutions:",
  "mtn: resolve_first_left drop",
  "mtn: resolve_first_left rename",
  "mtn: resolve_first_left user_rename \"new_content_name\" \"new_file_name\"",
  "mtn: resolve_first_left keep",
  "mtn: resolve_first_left user \"name\"",
  "mtn: resolve_first_right drop",
  "mtn: resolve_first_right rename",
  "mtn: resolve_first_right user_rename \"new_content_name\" \"new_file_name\"",
  "mtn: resolve_first_right keep",
  "mtn: resolve_first_right user \"name\""}))   

-- We want to keep the upstream node to avoid future conflicts
check(mtn("conflicts", "resolve_first_left", "keep"), 0, nil, true)

check(mtn("conflicts", "show_first"), 0, nil, true)
check(samelines("stderr",
 {"mtn: conflict: file 'file_2'",
  "mtn: modified on the left",
  "mtn: dropped and recreated on the right",
  "mtn: left_resolution: keep",
  "mtn: possible resolutions:",
  "mtn: resolve_first_right drop",
  "mtn: resolve_first_right rename",
  "mtn: resolve_first_right user_rename \"new_content_name\" \"new_file_name\"",
  "mtn: resolve_first_right keep",
  "mtn: resolve_first_right user \"name\""}))   

check(mtn("conflicts", "resolve_first_right", "drop"), 0, nil, true)
check(samefilestd("conflicts_3_2_resolved", "_MTN/conflicts"))

check(mtn("explicit_merge", "--resolve-conflicts", upstream_3, local_2, "testbranch"), 0, nil, true)
check(qgrep("mtn: dropping 'file_2'", "stderr"))

check(mtn("update"), 0, nil, true)
check(samelines("file_2", {"file_2 upstream 2"}))

-- end of file
