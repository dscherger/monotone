
check(get("example.key"))

mtn_setup()

check(mtn("read"), 0, false, false, { "example.key" })

-- fetch by name
check(mtn("privkey", "foo@bar.com"), 0, true, false)
check(samefile("example.key", "stdout"))

-- fetch by id
check(mtn("privkey", "d081c00cf730ee673d3d75e5a8262e2fec11a23f"), 0, true, false)
check(samefile("example.key", "stdout"))

-- fetch with absent database and workspace
check(nodb_mtn("privkey", "--no-workspace", "foo@bar.com"), 0, true, false)
check(samefile("example.key", "stdout"))

-- fetch non-existant key
check(mtn("privkey", "blabla"), 1, false, true)
check(qgrep("there is no key named 'blabla'", "stderr"))

