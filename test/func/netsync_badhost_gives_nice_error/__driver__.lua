skip_if(not existsonpath("host"))

-- We punt in case of misconfigured DNS servers that resolve inexistant
-- domain names. (Don't even think about buying that domain name!)
L("\nChecking DNS resolution for nosuchhost__blahblah__asdvasoih.com: ")
local pid = spawn_redirected("", "host-lookup.out", "host-lookup.err",
                             "host", "nosuchhost__blahblah__asdvasoih.com")
local ec = wait(pid)

if ec == 0 then
  L("failed\n",
    "\n",
    "Your DNS resolver is trying to be helpful by resolving names that do\n",
    "not exist. `host nosuchhost__blahblah__asdvasoih.com` returned:\n\n")
  log_file_contents("host-lookup.out")
  skip_if(true)
else
  L("good\n")
end

mtn_setup()

check(mtn("pull", "nosuchhost__blahblah__asdvasoih.com", "some.pattern"), 1, false, true)
check(qgrep("name resolution failure for nosuchhost__blahblah__asdvasoih.com", "stderr"))

check(mtn("pull", "mtn:localhost?*"), 1, false, true)
check(qgrep("a non-empty hostname is expected for the 'mtn' uri scheme", "stderr"))
