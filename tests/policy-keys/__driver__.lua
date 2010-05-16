mtn_setup()


-- this is a placeholder

-- test that keys are handled properly:
--   * naming keys by ID always works
--   * naming keys by given name only work if we have the private key
--   * local name comes only from policy
--     + should this name be prefixed with the policy path?

check(mtn("create_project", "test_project"), 0, false, false)

check(mtn("setup", "-btest_project.__policy__", "policy_checkout"), 0, false, false)

-- generate key; use in delegation, branch, and branch under delegation
check(mtn("genkey", "mykey@test.net"), 0, false, true,
      string.rep("mykey@test.net\n", 2))
for hash in readfile("stderr"):gmatch("'(" .. string.rep("%x", 40) .. ")'") do
   my_key = hash
end

mkdir("policy_checkout/keys")
writefile("policy_checkout/keys/my_key", my_key)
check(indir("policy_checkout", mtn("add", "keys/my_key", "--no-respect-ignore")),
   0, false, false)
check(indir("policy_checkout", mtn("commit", "-m", "add key")), 0, false, false)

check(mtn("create_subpolicy", "test_project.delegated",
	  "--no-workspace", "-k", "my_key"))
check(mtn("ls", "keys"))

-- drop private key (dropkey -d:memory:)
check(mtn("-d", ":memory:", "--no-workspace", "dropkey", my_key), 0, nil, false)

-- check that the delegation and branches still work


check(false)