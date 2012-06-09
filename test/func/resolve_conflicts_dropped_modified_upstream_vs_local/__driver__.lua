-- Show a problematic use case involving a dropped_modified conflict.
--
-- There is an upstream branch, and a local branch. The local branch
-- deletes a file that the upstream branch continues to modify. We
-- periodically merge from upstream to local to get other changes, but
-- never merge in the other direction.
--
-- The dropped file causes new dropped_modified conflicts at each
-- propagate. We decided to always drop; we'd like to be able to tell
-- mtn that somehow.

mtn_setup()

addfile("file_2", "file_2 base")
commit("testbranch", "base")
base = base_revision()

writefile("file_2", "file_2 upstream 1")

commit("testbranch", "upstream 1")
upstream_1 = base_revision()

revert_to(base)

check(mtn("drop", "file_2"), 0, false, false)

commit("testbranch", "local 1")
local_1 = base_revision()

check(mtn("show_conflicts", upstream_1, local_1), 0, nil, true)
check(samelines
("stderr",
 {"mtn: [left]     1e700864de7a2cbb1cf85c26f5e1e4ca335d2bc2",
  "mtn: [right]    a2889488ed1801a904d0219ec9939dfc2e9be033",
  "mtn: [ancestor] f80ff103551d0313647d6c84990bc9db6b158dac",
  "mtn: conflict: file 'file_2' from revision f80ff103551d0313647d6c84990bc9db6b158dac",
  "mtn: modified on the left, named file_2",
  "mtn: dropped on the right",
  "mtn: 1 conflict with supported resolutions."}))

check(mtn("conflicts", "store", upstream_1, local_1), 0, nil, true)

check(mtn("conflicts", "resolve_first", "drop"), 0, nil, true)

check(mtn("explicit_merge", "--resolve-conflicts", upstream_1, local_1, "testbranch"), 0, nil, true)
check(samelines
("stderr",
 {"mtn: [left]  1e700864de7a2cbb1cf85c26f5e1e4ca335d2bc2",
  "mtn: [right] a2889488ed1801a904d0219ec9939dfc2e9be033",
  "mtn: dropping 'file_2'",
  "mtn: [merged] dd1ba606b52fddb4431da3760ff65b65f6509a48"}))

check(mtn("update"), 0, nil, true)
check(not exists("file_2"))

local_2 = base_revision()

-- round 2; upstream modifies the file again
revert_to(upstream_1)

writefile("file_2", "file_2 upstream 2")

commit("testbranch", "upstream 2")
upstream_2 = base_revision()

check(mtn("show_conflicts", upstream_2, local_2), 0, nil, true)
check(samelines
("stderr",
 {"mtn: [left]     9bf6dcccb01b4566f2470acd0c6afa48f6eaef65",
  "mtn: [right]    dd1ba606b52fddb4431da3760ff65b65f6509a48",
  "mtn: [ancestor] 1e700864de7a2cbb1cf85c26f5e1e4ca335d2bc2",
  "mtn: conflict: file 'file_2' from revision 1e700864de7a2cbb1cf85c26f5e1e4ca335d2bc2",
  "mtn: modified on the left, named file_2",
  "mtn: dropped on the right",
  "mtn: 1 conflict with supported resolutions."}))

check(mtn("conflicts", "store", upstream_2, local_2), 0, nil, true)

check(mtn("conflicts", "resolve_first", "drop"), 0, nil, true)

check(mtn("explicit_merge", "--resolve-conflicts", upstream_2, local_2, "testbranch"), 0, nil, true)
check(qgrep("mtn: dropping 'file_2'", "stderr"))

check(mtn("update"), 0, nil, true)
check(not exists("file_2"))

-- end of file
