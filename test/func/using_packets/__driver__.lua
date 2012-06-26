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

writefile("file", "xyz")
check(mtnA("add", "file"), 0, nil, false)
check(mtnA("commit", "-m", "One"), 0, nil, false)
rev_one = base_revision()

writefile("file2", "file 2 getting in")
writefile("file", "ERASE")
check(mtnA("add", "file2"), 0, nil, false)
check(mtnA("commit", "-m", "Two"), 0, nil, false)
rev_two = base_revision()

writefile("file", "THIRD")
check(mtnA("commit", "-m", "Three"), 0, nil, false)
rev_three = base_revision()

-- There's no easy way to pipe the output of one mtn cmd to another
-- here, so we skip the toposort.
check(mtnA("automate", "select", "i:"), 0, true, nil)
check(samelines("stdout", {rev_three, rev_one, rev_two}))
-- show the rev ids, so we can copy them into monotone.texi
check(samelines("stdout",
{"299b021a6d426f2ff3b09fabcd165abe5d82f2b5", -- rev_three
 "2d0b9a10f73ce2445213506c2a0a81b8e910b923", -- rev_one
 "b2ae639e410e4970cab274a580e87d08adc87ee7"})) -- rev_two

-- Transfer rev_one to db B
-- first, get the file id
file_id_one = "66b27417d37e024c46526c2f6d358a754fc552f3"
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
file_id_two = "491cd4818acc21cb6fb3f8fd5676633d60a6b973"
file2_id_two = "ca085d538b534aa2f1746ac9a61c6f1d279356a4"
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

check(samefile("file", "test-b/file"))
check(samefile("file2", "test-b/file2"))

-- end of file
