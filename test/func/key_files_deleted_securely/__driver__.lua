mtn_setup()

-- create two keys, rename both, the first to a custom name,
-- the second to an old-style name without ident

check(mtn("genkey", "foobar"), 0, false, true, "\n\n")
check(grep("has hash", "stderr"), 0, true)
line = readfile("stdout")
keyid1 = string.sub(line, 29, 68)
check(rename("keys/foobar."..keyid1, "keys/something-else"))

check(mtn("genkey", "foobar", "--force-duplicate-key"), 0, false, true, "\n\n")
check(grep("has hash", "stderr"), 0, true)
line = readfile("stdout")
keyid2 = string.sub(line, 29, 68)
check(rename("keys/foobar."..keyid2, "keys/foobar"))

check(mtn("dropkey", keyid1), 1, false, true)
check(qgrep("expected key with id " ..keyid1.." in key file", "stderr"))

-- if the conflicting key is gone, ensure that dropkey properly deletes
-- the old key file, even if it has no keyid suffixed
remove("keys/something-else")
check(mtn("dropkey", "foobar"), 0, false, false)

