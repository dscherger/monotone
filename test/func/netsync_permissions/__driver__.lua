
include("/common/netsync.lua")
mtn_setup()
keys = {}
revs = {}

-- generate a new key
function genkey(keyname, mt)
  if mt == nil then mt = mtn end
  check(mt("genkey", keyname), 0, false, false, string.rep(keyname.."\n", 2))
end

keys.other = "other@test.net"
genkey(keys.other)

keys.byhash = "byhash@test.net"
genkey(keys.byhash)
check(mtn("ls", "keys"), 0, true)
check(grep(" byhash@test.net$", "stdout"), 0, true)
line = readfile("stdout")
byhash_hash = string.sub(line, 0, 40)


netsync.setup()

-- test with open security settings
check(get("open"))

addfile("badfile", "badfile")
commit("badbranch", "badfile")

copy("test.db", "clean.db")
copy("keys", "clean_keys")
function clean(n)
  if n == nil or n == 1 then n = "" end
  copy("clean.db", "test"..n..".db")
  remove("keys"..n)
  copy("clean_keys", "keys"..n)
end

addfile("testfile", "testfile")
commit("testbranch", "testfile")
revs.base = base_revision()

srv = netsync.start({"--confdir=open"}, nil, true)

-- anonymous pull 

clean(2)

srv:pull({"testbranch", "--key", ""})
check(mtn2("automate", "get_revision", revs.base), 0, true, true)

-- pull with default key

clean(2)

srv:pull("testbranch")
check(mtn2("automate", "get_revision", revs.base), 0, true, true)

-- pull with other key

clean(2)

srv:pull({"testbranch", "--key", keys.other})
check(mtn2("automate", "get_revision", revs.base), 0, true, true)

-- pull with unknown key falls back to anonymous

clean(2)

keys.unknown = "unknown@test.net"
genkey(keys.unknown, mtn2)
srv:pull({"testbranch", "--key", keys.unknown}, nil, 0)
check(mtn2("automate", "get_revision", revs.base), 0, true, true)

-- push with default key

copy("test.db", "test2.db")
remove("keys2")
copy("keys", "keys2")
revert_to(revs.base, mtn2)
addfile("default", "default", mtn2)
check(mtn2("commit", "--message", "default"), 0, false, false)
revs.default = base_revision()
srv:push("testbranch")

-- push with other key

revert_to(revs.base, mtn2)
addfile("other", "other", mtn2)
check(mtn2("commit", "--message", "other"), 0, false, false)
revs.other = base_revision()
srv:push({"testbranch", "--key", keys.other})

-- push with unknown key fails

revert_to(revs.base, mtn2)
addfile("unknown", "unknown", mtn2)
check(mtn2("commit", "--message", "unknown"), 0, false, false)
revs.unknown = base_revision()
genkey(keys.unknown, mtn2)
srv:push({"testbranch", "--key", keys.unknown}, nil, 1)

srv:stop()

check(mtn("automate", "get_revision", revs.default), 0, true, true)
check(mtn("automate", "get_revision", revs.other), 0, true, true)
check(mtn("automate", "get_revision", revs.unknown), 1, true, true)


------------------------------------------------------------------------
------------------------------------------------------------------------
------------------------------------------------------------------------
-- test with closed security settings
check(get("closed"))

-- setup by-hash line in permissions files
writefile("closed/write-permissions.d/tester-by-hash", byhash_hash.."\n")

readperm = readfile("closed/read-permissions.d/tester")
readperm = readperm .. 'continue "yes"\n'
writefile("closed/read-permissions.d/tester", readperm)
writefile("closed/read-permissions.d/tester-by-hash",
       'pattern "*"\nallow "' .. byhash_hash .. '"\n')

-- general setup
clean()
writefile("_MTN/revision", 
	  "format_version \"1\"\n\n"..
	  "new_manifest [0000000000000000000000000000000000000000]\n\n"..
	  "old_revision []\n")

addfile("testfile", "testfile", mtn2)
commit("testbranch", "testfile")
revs.base = base_revision()

srv = netsync.start({"--confdir=closed"}, nil, true)

-- anonymous pull fails

clean(2)

srv:pull({"testbranch", "--key", ""}, nil, 1)
check(mtn2("automate", "get_revision", revs.base), 1, true, true)

-- pull with default key

clean(2)

srv:pull("testbranch")
check(mtn2("automate", "get_revision", revs.base), 0, true, true)

-- pull with by-hash key

clean(2)

srv:pull({"testbranch", "--key", keys.byhash})
check(mtn2("automate", "get_revision", revs.base), 0, true, true)

-- pull with bad branch fails

clean(2)

srv:pull("badbranch", nil, 1)
check(mtn2("automate", "get_revision", revs.base), 1, true, true)

-- pull with other key fails

clean(2)

srv:pull({"testbranch", "--key", keys.other}, nil, 1)
check(mtn2("automate", "get_revision", revs.base), 1, true, true)

-- pull with unknown key fails

clean(2)

genkey(keys.unknown, mtn2)
srv:pull({"testbranch", "--key", keys.unknown}, nil, 1)
check(mtn2("automate", "get_revision", revs.base), 1, true, true)

-- push with default key

copy("test.db", "test2.db")
remove("keys2")
copy("keys", "keys2")
revert_to(revs.base, mtn2)
addfile("default", "default", mtn2)
check(mtn2("commit", "--message", "default"), 0, false, false)
revs.default = base_revision()
srv:push("testbranch")

-- push with by-hash key

revert_to(revs.base, mtn2)
addfile("by-hash", "by-hash", mtn2)
check(mtn2("commit", "--message", "by-hash"), 0, false, false)
revs.other = base_revision()
srv:push({"testbranch", "--key", keys.byhash}, nil, 0)

-- push with other key

revert_to(revs.base, mtn2)
addfile("other", "other", mtn2)
check(mtn2("commit", "--message", "other"), 0, false, false)
revs.other = base_revision()
srv:push({"testbranch", "--key", keys.other}, nil, 1)

-- push with unknown key fails

revert_to(revs.base, mtn2)
addfile("unknown", "unknown", mtn2)
check(mtn2("commit", "--message", "unknown"), 0, false, false)
revs.unknown = base_revision()
genkey(keys.unknown, mtn2)
srv:push({"testbranch", "--key", keys.unknown}, nil, 1)

srv:stop()

check(mtn("automate", "get_revision", revs.default), 0, true, true)
check(mtn("automate", "get_revision", revs.other), 1, true, true)
check(mtn("automate", "get_revision", revs.unknown), 1, true, true)
