-- commands that use a specific database must fail early and clearly
check(raw_mtn("ls", "branches"), 1, false, true)
check(qgrep("no database specified", "stderr"))

-- some commands (setup and clone specifically) might use a default
-- database and even create it beforehand
check(raw_mtn("setup", "-b", "foo", "."), 0, false, true)
check(qgrep("using default database ':default.mtn'", "stderr"))
check(remove("_MTN"))
check(remove("databases"))
