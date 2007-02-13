include("/common/netsync.lua")

-- with no monotone keys:
-- * (E) export monotone key
check(mtn("ssh_agent_export"), 1, false, false)

mtn_setup()

tkey = "happy@example.com"

-- with one monotone key:
-- * (ok) mtn ci with ssh-agent not running
addfile("some_file", "test")
check(mtn("ci", "--message", "commit msg"), 0, false, false)

-- * (E) export key with -k that does not exist
check(mtn("--key", "n@n.com", "ssh_agent_export"), 1, false, false)

-- * (ok) export key without -k
check(raw_mtn("--rcfile", test.root .. "/test_hooks.lua", -- "--nostd",
              "--db=" .. test.root .. "/test.db",
              "--keydir", test.root .. "/keys",
              "ssh_agent_export"), 0, false, false)

-- * (ok) export key with -k that does exist
check(mtn("--key", "tester@test.net", "ssh_agent_export"), 0, false, false)

-- * (ok) export monotone key with passphrase
check(mtn("ssh_agent_export"), 0, false, false, tkey .. "\n" .. tkey .. "\n")

-- * (ok) export monotone key without passphrase
check(mtn("ssh_agent_export", "id_monotone"), 0, false, false)
skip_if(not existsonpath("chmod"))
check({"chmod", "600", "id_monotone"}, 0, false, false)

skip_if(not existsonpath("ssh-agent"))
skip_if(ostype == "Windows")

function cleanup()
   check({"kill", os.getenv("SSH_AGENT_PID")}, 0, false, false)
   set_env("SSH_AGENT_PID", "")
   set_env("SSH_AUTH_SOCK", "")
end

check({"ssh-agent"}, 0, true, false)
for line in io.lines("stdout") do
   for k, v in string.gmatch(line, "([%w_]+)=([%w/\.-]+)") do
      set_env(k, v)
   end
end

-- * (ok) mtn ci with ssh-agent running with no keys
addfile("some_file2", "test")
check(mtn("ci", "--message", "commit msg"), 0, false, false)

skip_if(not existsonpath("ssh-add"))

-- * (N) mtn ci with no ssh key with --ssh-sign
addfile("some_file3", "test")
check(mtn("ci", "--message", "commit msg", "--ssh-sign"), 1, false, false)

-- * (N) mtn ci with no ssh key with --ssh-sign=blah
addfile("some_file3", "test")
check(mtn("ci", "--message", "commit msg", "--ssh-sign=blah"), 1, false, false)

-- * (N) mtn ci with no ssh key with --ssh-sign=only
addfile("some_file3_b", "test")
check(mtn("ci", "--ssh-sign=only", "--message", "commit msg"), 1, false, false)

-- * (ok) mtn ci with no ssh key with --ssh-sign=yes
addfile("some_file4", "test")
check(mtn("ci", "--ssh-sign=yes", "--message", "commit msg"), 0, false, false)

-- * (ok) mtn ci with no ssh key with --ssh-sign=no
addfile("some_file5", "test")
check(mtn("ci", "--ssh-sign=no", "--message", "commit msg"), 0, false, false)

-- * (ok) mtn ci with no ssh key with --ssh-sign=check
addfile("some_file6", "test")
check(mtn("ci", "--ssh-sign=check", "--message", "commit msg"), 0, false, false)

-- * (ok) mtn ci with ssh-agent running with non-monotone rsa key
check(get("id_rsa"))
check({"chmod", "600", "id_rsa"}, 0, false, false)
check({"ssh-add", "id_rsa"}, 0, false, false)
addfile("some_file7", "test")
check(mtn("ci", "--message", "commit msg"), 0, false, false)

-- * (ok) mtn ci with ssh-agent running with dss key
check({"ssh-add", "-D"}, 0, false, false)
check(get("id_dsa"))
check({"chmod", "600", "id_dsa"}, 0, false, false)
check({"ssh-add", "id_dsa"}, 0, false, false)
addfile("some_file8", "test")
check(mtn("ci", "--message", "commit msg"), 0, false, false)

-- * (ok) mtn ci with ssh-agent running with multiple non-monotone rsa keys
check({"ssh-add", "-D"}, 0, false, false)
check({"ssh-add", "id_rsa"}, 0, false, false)
check(get("id_rsa2"))
check({"chmod", "600", "id_rsa2"}, 0, false, false)
check({"ssh-add", "id_rsa2"}, 0, false, false)
addfile("some_file9", "test")
check(mtn("ci", "--message", "commit msg"), 0, false, false)

--  * (ok) add password-less exported key with ssh-add
check({"ssh-add", "-D"}, 0, false, false)
check({"ssh-add", "id_monotone"}, 0, false, false)

--  * (ok) mtn ci with ssh key without --ssh-sign
addfile("some_file10", "test")
check(mtn("ci", "--message", "commit msg"), 0, false, false)

--  * (ok) mtn ci with ssh key with --ssh-sign=only
addfile("some_file11", "test")
check(mtn("ci", "--ssh-sign=only", "--message", "commit msg"), 0, false, false)

--  * (ok) mtn ci with ssh key with --ssh-sign=yes
addfile("some_file12", "test")
check(mtn("ci", "--ssh-sign=yes", "--message", "commit msg"), 0, false, false)

--  * (ok) mtn ci with ssh key with --ssh-sign=no
addfile("some_file13", "test")
check(mtn("ci", "--ssh-sign=no", "--message", "commit msg"), 0, false, false)

--  * (ok) mtn ci with ssh key with --ssh-sign=check
addfile("some_file14", "test")
check(mtn("ci", "--ssh-sign=check", "--message", "commit msg"), 0, false, false)

-- 
-- with multiple monotone keys:
check(mtn("genkey", "test2@tester.net"), 0, false, false)

-- * (N)  try to export monotone key without -k
remove("_MTN/options")
check(raw_mtn("--rcfile", test.root .. "/test_hooks.lua", -- "--nostd",
              "--db=" .. test.root .. "/test.db",
              "--keydir", test.root .. "/keys",
              "ssh_agent_export"), 1, false, false)

-- * (ok) export monotone key with -k
check(mtn("ssh_agent_export", "--key", "test2@tester.net", "id_monotone2"), 0, false, false)
skip_if(not existsonpath("chmod"))
check({"chmod", "600", "id_monotone2"}, 0, false, false)

-- * (ok) mtn ci with -k and with ssh-agent running with no keys
check({"ssh-add", "-D"}, 0, false, false)
addfile("some_file15", "test")
check(mtn("ci", "--key", "tester@test.net", "--message", "commit msg"), 0, false, false)

-- * (ok) mtn ci with -k and with ssh-agent running with one non-monotone rsa key
check({"ssh-add", "id_rsa"}, 0, false, false)
addfile("some_file16", "test")
check(mtn("ci", "--key", "tester@test.net", "--message", "commit msg"), 0, false, false)

-- * (ok) mtn ci with -k and with ssh-agent running with same monotone key ex/imported key
check({"ssh-add", "id_monotone"}, 0, false, false)
addfile("some_file17", "test")
check(mtn("ci", "--ssh-sign", "only", "--key", "tester@test.net", "--message", "commit msg"), 0, false, false)

-- * (ok) mtn ci with -k and with ssh-agent running with other monotone key ex/imported key
addfile("some_file18", "test")
check(mtn("ci", "--ssh-sign", "only", "--key", "test2@tester.net", "--message", "commit msg"), 1, false, false)
check(mtn("ci", "--key", "test2@tester.net", "--message", "commit msg"), 0, false, false)

-- * (ok) mtn ci with -k and with ssh-agent running with both montone keys ex/imported key
check({"ssh-add", "id_monotone2"}, 0, false, false)
addfile("some_file19", "test")
check(mtn("ci", "--key", "test2@tester.net", "--message", "commit msg"), 0, false, false)
addfile("some_file20", "test")
check(mtn("ci", "--key", "tester@test.net", "--message", "commit msg"), 0, false, false)
