mtn_setup()


-- this is a placeholder

-- test that keys are handled properly:
--   * naming keys by ID always works
--   * naming keys by given name only work if we have the private key
--   * local name comes only from policy
--     + should this name be prefixed with the policy path?

check(mtn("genkey", "mykey@test.net"), 0, false, true,
      string.rep("mykey@test.net\n", 2))
for hash in readfile("stderr"):gmatch("'(" .. string.rep("%x", 40) .. ")'") do
   my_key = hash
end
check(mtn("genkey", "otherkey@test.net"), 0, false, true,
      string.rep("otherkey@test.net\n", 2))
for hash in readfile("stderr"):gmatch("'(" .. string.rep("%x", 40) .. ")'") do
   other_key = hash
end

function init_policy(project, key, name, hash)
   check(mtn("create_project", project, "-k", key), 0, false, false)

   check(mtn("setup", "-b", project .. ".__policy__", project),
	 0, false, false)

   mkdir(project .. "/keys")
   writefile(project .. "/keys/" .. name, hash)
   check(indir(project, mtn("add", "keys/" .. name, "--no-respect-ignore")),
	 0, false, false)
   check(indir(project, mtn("commit", "-m", "add key", "-k", key)),
	 0, false, false)
end

init_policy("test_project", "mykey@test.net", "my_key", my_key)
init_policy("other_project", "otherkey@test.net", "other_key", other_key)


--  use new key in delegation, branch, and branch under delegation

check(mtn("ls", "keys"), 0, true)
check(qgrep("test_project.my_key", "stdout"))


check(mtn("create_subpolicy", "test_project.delegated",
	  "--no-workspace", "-k", "my_key"), 0, nil, false)

check(mtn("create_branch", "test_project.somebranch",
	  "--no-workspace", "-k", "my_key"), 0, nil, false)

check(mtn("create_branch", "test_project.delegated.otherbranch",
	  "--no-workspace", "-k", "my_key"), 0, nil, false)

-- unrelated keys don't work
check(mtn("create_branch", "test_project.badbranch",
	  "--no-workspace", "-k", "other_key"), 1, nil, false)

-- drop private key (dropkey -d:memory:)
check(mtn("-d", ":memory:", "--no-workspace", "dropkey", my_key), 0, nil, false)

-- check that the delegation and branches still work


check(false)