-- In mtn 0.46 .. 0.47, branch_leaves table was corrupted by this scenario:
--
-- Two developers working on the same branch do the same merge, one
-- adds another commit, then they sync
--
-- The bug does not occur if the two workspaces use the same author
-- name and key.

function abe_mtn(...)
  return raw_mtn("--rcfile", test.root .. "/min_hooks.lua",
         "--db=" .. test.root .. "/abe.db",
         "--keydir", test.root .. "/keys",
         "--key=abe@test.net",
         ...)
end

function beth_mtn(...)
  return raw_mtn("--rcfile", test.root .. "/min_hooks.lua",
         "--db=" .. test.root .. "/beth.db",
         "--keydir", test.root .. "/keys",
         "--key=beth@test.net",
         ...)
end

-- we don't use 'mtn_setup()', because we want two separate developers
check(getstd("min_hooks.lua"))
mkdir("Abe")
check(abe_mtn("db", "init"), 0, false, false)
copy("abe.db", "beth.db")

chdir("Abe")
check(abe_mtn("genkey", "abe@test.net"), 0, false, false, string.rep("abe@test.net\n", 2))
check(abe_mtn("setup", "--branch", "testbranch"), 0, false, false)

addfile("file1", "base", abe_mtn)
commit("testbranch", "base", abe_mtn)
base = base_revision()

-- Create Beth's workspace via checkout, so 'update' works
chdir(test.root)
check(abe_mtn("sync", "file://" .. test.root .. "/beth.db", "*"), 0, false, false)
check(beth_mtn("checkout", "--branch", "testbranch", "Beth"), 0, false, false)
chdir("Beth")
check(beth_mtn("genkey", "beth@test.net"), 0, false, false, string.rep("beth@test.net\n", 2))

-- Abe creates two heads on testbranch
chdir("../Abe")
writefile("file1", "rev_a")
commit("testbranch", "rev_a", abe_mtn)
rev_a = base_revision()

revert_to(base, "testbranch", abe_mtn)

addfile("file2", "rev_b", abe_mtn)
commit("testbranch", "rev_b", abe_mtn)
rev_b = base_revision()

-- Sync dbs
check(abe_mtn("sync", "file://" .. test.root .. "/beth.db", "*"), 0, false, false)

-- Abe merges
chdir("Abe")
check(abe_mtn("merge", "--message", "rev_c"), 0, false, false)

-- Beth merges, and adds another revision
chdir("../Beth")
check(beth_mtn("merge", "--message", "rev_c"), 0, false, false)
check(beth_mtn("update"), 0, false, false)

writefile("file2", "rev_d", beth_mtn)
commit("testbranch", "rev_d", beth_mtn)
rev_d = base_revision()

-- Sync dbs (not clear if direction of sync matters)
check(beth_mtn("sync", "file://" .. test.root .. "/abe.db", "*"), 0, false, false)

-- bug; rev_d and rev_c are both heads according to branch_leaves table.
check(beth_mtn("db", "check"), 0, false, false)

-- end of file
