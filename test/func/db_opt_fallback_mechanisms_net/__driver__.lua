skip_if(no_network_tests)

-- This test is also network dependent. Check if we can resolve DNS
-- names, before trying the following test.
skip_if(not existsonpath("host"))

L("\nChecking DNS resolution for code.monotone.ca: ")
local pid = spawn_redirected("", "host-lookup.out", "host-lookup.err",
	  "host", "code.monotone.ca")
local ec = wait(pid)
skip_if(ec ~= 0)

-- and some commands should use :memory: as default because they
-- just need a temporary throw-away database to work properly
check(raw_mtn("au", "remote", "interface_version", "--remote-stdio-host", "http://code.monotone.ca/monotone", "--key="), 0, false, true)
check(qgrep("no database given; assuming ':memory:' database", "stderr"))
