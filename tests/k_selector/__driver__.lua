mtn_setup()

check(get("rcfile"))

function selmap(sel, revs, sort)
  check(raw_mtn("automate", "select", sel, "--rcfile", "rcfile"), 0, true, false)
  if sort ~= false then table.sort(revs) end
  check(samelines("stdout", revs))
end

check(mtn("genkey", "local_name_test@test.net"), 0, false, false, "\n\n")

check(mtn("au", "keys"), 0, true, false)
local basicio = parse_basic_io(readfile("stdout"))
local hash = nil
for _,l in pairs(basicio) do
  if l.name == "hash" then hash = l.values[1] end
  if l.name == "given_name" and
     l.values[1] == "local_name_test@test.net" then break end
end
check(hash ~= nil)

addfile("testfile", "blah blah")
commit()
REV1=base_revision()

writefile("testfile", "stuff stuff")
check(mtn("commit", "-k", "local_name_test@test.net", "-m", "test"), 0, false, false)
REV2=base_revision()

-- negative checks
check(mtn("au", "select", "k:"), 1, false, true)
check(qgrep("must not be empty", "stderr"))

check(mtn("au", "select", "k:tester"), 1, false, true)
check(qgrep("there is no key named", "stderr"))

check(mtn("au", "select", "k:1000000000000000000000000000000000000001"), 1, false, true)
check(qgrep("there is no key named", "stderr"))

-- positive checks
selmap("k:tester@test.net", {REV1})
-- if the user chosed a local name for a specific key, we can't query
-- the key by its original given name anywhere. this is actually wanted
-- in case the user chosed a local name for one of two keys with the
-- same given names which would otherwise not be distinguishable (beside
-- their hash id)
selmap("k:custom_name@test.net", {REV2})
selmap("k:" .. hash, {REV2})

