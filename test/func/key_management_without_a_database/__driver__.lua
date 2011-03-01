
mtn_setup()

-- with no database should work
check(raw_mtn("--keydir=keys", "--no-workspace",
	      "genkey", "foobar"), 0, false, false,
      string.rep("foobar\n", 2))

check(raw_mtn("--keydir=keys", "--no-workspace",
	      "passphrase", "foobar"), 0, false, false,
      "foobar\n"..string.rep("barfoo\n", 2))

check(raw_mtn("--keydir=keys", "--no-workspace",
	      "ls", "keys"), 0, false, false)

check(raw_mtn("--keydir=keys", "--no-workspace",
	      "pubkey", "foobar"), 0, false, false)

check(raw_mtn("--keydir=keys", "--no-workspace",
	      "privkey", "foobar"), 0, false, false)

check(raw_mtn("--keydir=keys", "--no-workspace",
	      "ssh_agent_export", "--key", "foobar"), 0, false, false,
      "barfoo\n")

check(raw_mtn("--keydir=keys", "--no-workspace",
	      "dropkey", "foobar"), 0, false, false)

-- now with the automate interface
check(raw_mtn("--keydir=keys", "--no-workspace",
	      "au", "generate_key", "foobar", "foobar"), 0, false, false)

check(raw_mtn("--keydir=keys", "--no-workspace",
	      "au", "keys"), 0, false, false)

check(raw_mtn("--keydir=keys", "--no-workspace",
	      "au", "get_public_key", "foobar"), 0, false, false)

check(raw_mtn("--keydir=keys", "--no-workspace",
	      "dropkey", "foobar"), 0, false, false)

-- with an invalid database should fail
check(raw_mtn("--keydir=keys", "--db=bork", "genkey", "foo@baz"), 1, false, false, string.rep("foo@baz\n", 2))
