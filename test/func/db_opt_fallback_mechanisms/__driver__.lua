-- commands that use a specific database must fail early and clearly
check(raw_mtn("ls", "branches"), 1, false, true)
check(qgrep("no database specified", "stderr"))

-- some commands (setup and clone specifically) might use a default
-- database and even create it beforehand
check(raw_mtn("setup", "-b", "foo", "."), 0, false, true)
check(qgrep("using default database ':default.mtn'", "stderr"))
check(remove("_MTN"))
check(remove("databases"))

skip_if(no_network_tests)

-- and some commands should use :memory: as default because they
-- just need a temporary throw-away database to work properly
check(raw_mtn("au", "remote", "interface_version", "--remote-stdio-host", "http://code.monotone.ca/monotone", "--key="), 0, false, true)
check(qgrep("no database given; assuming ':memory:' database", "stderr"))
