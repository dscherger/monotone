-- Test for "Using packets" example in manual; also issue 185

-- We need two databases A and B, and one workspace '.'.

function mtnA(...)
  return mtn("--db=A.db", ...)
end

function mtnB(...)
  return mtn("--db=B.db", ...)
end

-- initialize the databases, setup the workspace

-- copied from ../../func-testsuite.lua mtn_setup
check(getstd("test_keys"))
check(getstd("test_hooks.lua"))
check(getstd("min_hooks.lua"))

check(mtnA("db", "init"), 0, false, false)
check(mtnB("db", "init"), 0, false, false)

check(mtnA("read", "test_keys"), 0, false, false)
remove("test_keys")

check(mtnA("setup", "--branch=test", "."), 0, false, false)

-- add revisions to A

writefile("file", "xyz\n")
check(mtnA("add", "file"), 0, nil, false)
check(mtnA("commit", "-m", "One"), 0, nil, false)
rev_one = base_revision()

writefile("file2", "file 2 getting in\n")
writefile("file", "ERASE\n")
check(mtnA("add", "file2"), 0, nil, false)
check(mtnA("commit", "-m", "Two"), 0, nil, false)
rev_two = base_revision()

writefile("file", "THIRD\n")
check(mtnA("commit", "-m", "Three"), 0, nil, false)
rev_three = base_revision()

-- There's no easy way to pipe the output of one mtn cmd to another
-- here, so we skip the toposort.
check(mtnA("automate", "select", "i:"), 0, true, nil)
check(samelines("stdout", {rev_three, rev_one, rev_two}))
-- show the rev ids, so we can copy them into monotone.texi
check(samelines("stdout",
{"151f1fb125f19ebe11eb8bfe3a5798fcbea4e736", -- rev_three
 "a423db0ad651c74e41ab2529eca6f17513ccf714", -- rev_one
 "d14e89582ad9030e1eb62f563c8721be02ca0b65"})) -- rev_two

-- Transfer rev_one to db B
-- first, get the file id
file_id_one = "8714e0ef31edb00e33683f575274379955b3526c"
check(mtnA("automate", "get_revision", rev_one), 0, true, nil)
check(qgrep(file_id_one, "stdout"))

-- we don't have Lua functions to add lines to files, so we create
-- several packet files.

check(mtnA("pubkey", "tester@test.net"), 0, true, nil)
rename("stdout", "KEY_PACKETS")

check(mtnA("automate", "packet_for_fdata", file_id_one), 0, true, nil)
rename("stdout", "PACKET1")

check(mtnA("automate", "packet_for_rdata", rev_one), 0, true, nil)
rename("stdout", "PACKET2")

check(mtnA("automate", "packets_for_certs", rev_one), 0, true, nil)
rename("stdout", "PACKET3")

check(mtnB("read", "KEY_PACKETS", "PACKET1", "PACKET2", "PACKET3"), 0, nil, false)

-- Now transfer rev_two
file_id_two = "8b52d96d4fab6c1e56d6364b0a2673f4111b228e"
file2_id_two = "d2178687226560032947c1deacb39d16a16ea5c6"
check(mtnA("automate", "get_revision", rev_two), 0, true, nil)
check(qgrep(file_id_two, "stdout"))
check(qgrep(file2_id_two, "stdout"))

check(mtnA("automate", "packet_for_fdata", file2_id_two), 0, true, nil)
rename("stdout", "PACKET1")

check(mtnA("automate", "packet_for_fdelta", file_id_one, file_id_two), 0, true, nil)
rename("stdout", "PACKET2")

check(mtnA("automate", "packet_for_rdata", rev_two), 0, true, nil)
rename("stdout", "PACKET3")

check(mtnA("automate", "packets_for_certs", rev_two), 0, true, nil)
rename("stdout", "PACKET4")

check(mtnB("read", "PACKET1", "PACKET2", "PACKET3", "PACKET4"), 0, nil, false)

--  Compare checkout of rev_two from db A to rev_two from db B
check(mtnA("update", "-r", rev_two), 0, nil, false)

check(mtnB("checkout", "--branch", "test", "test-B"), 0, nil, false)

check(samefile("file", "test-B/file"))
check(samefile("file2", "test-B/file2"))

-- end of file
