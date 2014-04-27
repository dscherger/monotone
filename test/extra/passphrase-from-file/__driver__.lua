mtn_setup()

-- Prepare the test by copying the lua hook to test
-- and adding a few lines to test_hooks.lua
mkdir("hooks.d")
check(copy(srcdir.."/../extra/mtn-hooks/get_passphrase_from_file.lua",
	   "hooks.d/get_passphrase_from_file.lua"))

append("test_hooks.lua", "\
\
includedirpattern(get_confdir() .. \"/hooks.d\",\"*.conf\")\
includedirpattern(get_confdir() .. \"/hooks.d\",\"*.lua\")\
")

-- Load the extra key that needs a pass phrase
check(get("tester-key-with-passphrase"))
check(mtn("read","tester-key-with-passphrase"), 0, false, false)

-- Write the passphrase file.
writefile("passphrases", "tester-pwd@test.net \"f00bar\"\n")

-- Do the test
addfile("test1", "foo")
check(raw_mtn("--keydir", test.root .. "/keys",
	      "--key=tester@test.net",
	      "commit", "--message", "blah-blah", "--branch", "testbranch"),
      1, false, false)
check(nokey_mtn("commit", "--message", "blah-blah", "--branch", "testbranch",
		"--key", "tester-pwd@test.net"), 0, false, false)
