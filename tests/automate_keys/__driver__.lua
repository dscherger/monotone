
mtn_setup()

addfile("testfile", "foo bar")
check(mtn("ci", "-m", "foobar"), 0, false, false)
check(mtn("genkey", "foo@bar.com"), 0, false, false, string.rep("foo@bar.com\n", 2))
check(mtn("genkey", "foo@baz.com"), 0, false, false, string.rep("foo@baz.com\n", 2))
check(mtn("pubkey", "foo@baz.com"), 0, true)
rename("stdout", "baz")
check(mtn("dropkey", "foo@baz.com"), 0, false, false)
check(mtn("read"), 0, false, false, {"baz"})

-- we now have foo@bar.com in the keystore, tester@test.net in both keystore
-- and database, and foo@baz.com in only the database
check(mtn("automate", "keys"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))
locs = {}
for _,line in pairs(parsed) do
  if line.name == "name" then
    key = line.values[1]
    locs[key] = {db = false, ks = false, pub = false, priv = false}
  end
  if string.find(line.name, "location") then
    for _,v in pairs(line.values) do
      if v == "keystore" then locs[key].ks = true end
      if v == "database" then locs[key].db = true end
    end
  end
  if string.find(line.name, "private") then locs[key].priv = true end
  if string.find(line.name, "public") then locs[key].pub = true end
end
check(locs["foo@bar.com"].db == false)
check(locs["foo@bar.com"].ks == true)
check(locs["foo@baz.com"].priv == false)
check(locs["foo@baz.com"].pub == true)
check(locs["tester@test.net"].db == true)
check(locs["tester@test.net"].ks == true)
check(locs["tester@test.net"].pub == true)
check(locs["tester@test.net"].priv == true)
