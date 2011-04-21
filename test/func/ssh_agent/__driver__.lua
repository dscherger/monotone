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
check(raw_mtn("--rcfile", test.root .. "/test_hooks.lua", -- "--no-builtin-rcfiles",
              "--db=" .. test.root .. "/test.db",
              "--keydir", test.root .. "/keys",
              "ssh_agent_export"), 0, false, false)

-- * (ok) export key with -k that does exist
check(mtn("--key", "tester@test.net", "ssh_agent_export"), 0, false, false)

-- * (ok) export monotone key with passphrase
check(mtn("ssh_agent_export"), 0, false, false, tkey .. "\n" .. tkey .. "\n")

-- * (ok) export monotone key without passphrase
check(mtn("ssh_agent_export"), 0, false, false)

-- * (ok) export in workspace exports to subdir
mkdir("subdir")
mkdir("subdir/anotherdir")
writefile("subdir/foo", "data data")
writefile("subdir/anotherdir/bar", "more data")
chdir("subdir")
check(mtn("add", "foo"), 0, false, false)
check(mtn("add", "-R", "anotherdir"), 0, false, false)

check(mtn("ssh_agent_export", "id_monotone"), 0, false, false)
check(exists("id_monotone"))
chdir("..")
check(not exists("id_monotone"))

-- * (ok) export to subdirectory named on command line
check(mtn("ssh_agent_export", "subdir/id_monotone2"), 0, false, false)
check(exists("subdir/id_monotone2"))
check(not exists("id_monotone2"))

-- * (ok) export to path containing a nonexistent directory - creates it
check(not exists("nonexistent"))
check(mtn("ssh_agent_export", "nonexistent/id_monotone3"), 0, false, false)
check(exists("nonexistent/id_monotone3"))
check(not exists("id_monotone3"))

-- * (E) export to path that's not writable
-- we don't know how to do this on windows

-- Cygwin doesn't do write permissions properly
skip_if(string.sub(ostype, 1, 6)=="CYGWIN")
skip_if(ostype == "Windows") -- chmod doesn't work
skip_if(not existsonpath("chmod"))

mkdir("unwritable")
check({"chmod", "555", "unwritable"}, 0, false, false)
check(mtn("ssh_agent_export", "unwritable/id_monotone4"), 1, false, false)
check(not exists("unwritable/id_monotone3"))
check(not exists("id_monotone3"))

-- set up for tests of the agent itself
-- we don't know how to do this on windows

skip_if(ostype == "Windows")
skip_if(not existsonpath("ssh-agent"))
skip_if(not existsonpath("ssh-add"))

function cleanup()
   check({"kill", os.getenv("SSH_AGENT_PID")}, 0, false, false)
end

check({"ssh-agent"}, 0, true, false)
for line in io.lines("stdout") do
   for k, v in string.gmatch(line, "([%w_]+)=([%w/\.-]+)") do
      set_env(k, v)
   end
end

-- * (ok) mtn ssh_agent_add adds key to agent
check(mtn("ssh_agent_add"), 0, false, false)
check({"ssh-add", "-l"}, 0, true, false)
ok = false
for line in io.lines("stdout") do
    for k in string.gmatch(line, "tester@test\.net") do
    	ok = true
    end
end
if not ok then
   err("identity was not added to ssh-agent")
end

-- * (ok) mtn ci with ssh-agent running with no keys
check({"ssh-add", "-D"}, 0, false, false)
addfile("some_file2", "test")
check(mtn("ci", "--message", "commit msg"), 0, false, false)

-- key should be auto-added in ssh-agent
check({"ssh-add", "-l"}, 0, true, false)
for line in io.lines("stdout") do
    for k in string.gmatch(line, "no identities") do
       err("no identity in ssh-agent when there should be one")
    end
end

-- * (N) mtn ci with no ssh key with --ssh-sign
check({"ssh-add", "-D"}, 0, false, false) addfile("some_file3", "test")
check(mtn("ci", "--message", "commit msg", "--ssh-sign"), 1, false, false)

-- * (N) mtn ci with no ssh key with --ssh-sign=blah
check({"ssh-add", "-D"}, 0, false, false)
addfile("some_file3", "test")
check(mtn("ci", "--message", "commit msg", "--ssh-sign=blah"), 1, false, false)

-- * (N) mtn ci with no ssh key with --ssh-sign=only
check({"ssh-add", "-D"}, 0, false, false)
addfile("some_file3_b", "test")
check(mtn("ci", "--debug", "--ssh-sign=only", "--message", "commit msg"), 1, false, false)

-- key should not be in ssh-agent with --ssh-sign=only
check({"ssh-add", "-l"}, 1, false, false)
--for line in io.lines("stdout") do
--    if not string.gmatch(line, "no identities") then
--       err("identity in ssh-agent when there should be none")
--    end
--end

-- * (ok) mtn ci with no ssh key with --ssh-sign=yes
check({"ssh-add", "-D"}, 0, false, false)
addfile("some_file4", "test")
check(mtn("ci", "--ssh-sign=yes", "--message", "commit msg"), 0, false, false)

-- key should be auto-added in ssh-agent with --ssh-sign=yes
check({"ssh-add", "-l"}, 0, true, false)
for line in io.lines("stdout") do
    for k in string.gmatch(line, "no identities") do
       err("no identity in ssh-agent when there should be one")
    end
end

-- * (ok) mtn ci with no ssh key with --ssh-sign=no
check({"ssh-add", "-D"}, 0, false, false)
addfile("some_file5", "test")
check(mtn("ci", "--ssh-sign=no", "--message", "commit msg"), 0, false, false)

-- key should not be in ssh-agent with --ssh-sign=no
check({"ssh-add", "-l"}, 1, false, false)

-- * (ok) mtn ci with no ssh key with --ssh-sign=check
check({"ssh-add", "-D"}, 0, false, false)
addfile("some_file6", "test")
check(mtn("ci", "--ssh-sign=check", "--message", "commit msg"), 0, false, false)

-- key should not be auto-added in ssh-agent with --ssh-sign=check
check({"ssh-add", "-l"}, 0, true, false)
for line in io.lines("stdout") do
    for k in string.gmatch(line, "no identities") do
       err("no identity in ssh-agent when there should be one")
    end
end

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

-- * (ok) export key with password
check(mtn("ssh_agent_export", "id_monotone_pass"), 0, false, false,
      "pass\npass\n")

-- without any password, or the wrong password, ssh-add should not add
-- this key; the DISPLAY/SSH_ASKPASS subterfuge is necessary to get
-- ssh-add to read the password from somewhere other than /dev/tty.
save_env()
set_env("DISPLAY", "not-a-real-display")
set_env("SSH_ASKPASS", test.root .. "/report_pass")

check({"ssh-add", "-D"}, 0, false, false)

-- * (E) no password at all
writefile("report_pass", "#!/bin/sh\nexit 0")
check({"chmod", "700", "report_pass"}, 0, false, false)
check({"ssh-add", "id_monotone_pass"}, 1, false, false)

-- * (E) wrong password (note that ssh-add will retry indefinitely as long
-- as ssh-askpass keeps giving it (success, wrong password))
writefile("report_pass", 
	  "#!/bin/sh\n"..
          "if [ -f report_pass_retried ]; then\n"..
	  "  exit 1\n"..
	  "else\n"..
	  "  echo notpass\n"..
	  "  touch report_pass_retried\n"..
	  "  exit 0\n"..
	  "fi\n")
check({"chmod", "700", "report_pass"}, 0, false, false)
check({"ssh-add", "id_monotone_pass"}, 1, false, false)

-- * (ok) correct password
writefile("report_pass", "#!/bin/sh\necho pass\nexit 0")
check({"chmod", "700", "report_pass"}, 0, false, false)
check({"ssh-add", "id_monotone_pass"}, 0, false, false)

restore_env()

--  * (E) export key with mismatched passwords
check(mtn("ssh_agent_export", "id_mismatched_pass"), 1, false, false,
      "this\nthat\nthis\nthat\nthis\nthat")
check(not exists("id_mismatched_pass"))

--  * (ok) add password-less exported key with ssh-add
check(mtn("ssh_agent_export", "id_monotone"), 0, false, false)
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
check(raw_mtn("--rcfile", test.root .. "/test_hooks.lua", -- "--no-builtin-rcfiles",
              "--db=" .. test.root .. "/test.db",
              "--keydir", test.root .. "/keys",
              "ssh_agent_export"), 1, false, false)

-- * (N)  try to add monotone key without -k
remove("_MTN/options")
check(raw_mtn("--rcfile", test.root .. "/test_hooks.lua", -- "--no-builtin-rcfiles",
              "--db=" .. test.root .. "/test.db",
              "--keydir", test.root .. "/keys",
              "ssh_agent_add"), 1, false, false)

-- * (ok) export monotone key with -k
check(mtn("ssh_agent_export", "--key", "test2@tester.net", "id_monotone2"), 0, false, false)

-- * (ok) mtn ssh_agent_add with -k adds key to agent
check({"ssh-add", "-D"}, 0, false, false)
check(mtn("ssh_agent_add", "--key", "test2@tester.net"), 0, false, false)
check({"ssh-add", "-l"}, 0, true, false)
ok = false
for line in io.lines("stdout") do
    for k in string.gmatch(line, "test2@tester\.net") do
    	ok = true
    end
end
if not ok then
   err("identity was not added to ssh-agent")
end

-- * (ok) mtn ci with -k and with ssh-agent running with no keys
check({"ssh-add", "-D"}, 0, false, false)
addfile("some_file15", "test")
check(mtn("ci", "--key", "tester@test.net", "--message", "commit msg"), 0, false, false)

-- * (ok) mtn ci with -k and with ssh-agent running with one non-monotone rsa key
check({"ssh-add", "-D"}, 0, false, false)
check({"ssh-add", "id_rsa"}, 0, false, false)
addfile("some_file16", "test")
check(mtn("ci", "--key", "tester@test.net", "--message", "commit msg"), 0, false, false)

-- * (ok) mtn ci with -k and with ssh-agent running with same monotone key ex/imported key
check({"ssh-add", "-D"}, 0, false, false)
check({"ssh-add", "id_monotone"}, 0, false, false)
addfile("some_file17", "test")
check(mtn("ci", "--ssh-sign", "only", "--key", "tester@test.net", "--message", "commit msg"), 0, false, false)

-- * (ok) mtn ci with -k and with ssh-agent running with other monotone key ex/imported key
addfile("some_file18", "test")
check(mtn("ci", "--ssh-sign", "only", "--key", "test2@tester.net", "--message", "commit msg"), 1, false, false)
check(mtn("ci", "--key", "test2@tester.net", "--message", "commit msg"), 0, false, false)

-- * (ok) mtn ci with -k and with ssh-agent running with both montone keys ex/imported key
check({"ssh-add", "-D"}, 0, false, false)
check({"ssh-add", "id_monotone"}, 0, false, false)
check({"ssh-add", "id_monotone2"}, 0, false, false)
addfile("some_file19", "test")
check(mtn("ci", "--key", "test2@tester.net", "--message", "commit msg"), 0, false, false)
addfile("some_file20", "test")
check(mtn("ci", "--key", "tester@test.net", "--message", "commit msg"), 0, false, false)

-- * (ok) create passworded key and export it
check({"ssh-add", "-D"}, 0, false, false)
check(mtn("genkey", "test_pass@tester.net"), 0, false, false, "pass\npass\n")
check(mtn("ssh_agent_export", "--key", "test_pass@tester.net"), 0, false, false, "pass\npass2\npass2\n")

-- * (ok) add passworded key
check({"ssh-add", "-D"}, 0, false, false)
check(mtn("ssh_agent_add", "--key", "test_pass@tester.net"), 0, false, false, "pass\n")
check({"ssh-add", "-l"}, 0, true, false)
ok = false
for line in io.lines("stdout") do
    for k in string.gmatch(line, "test_pass@tester\.net") do
    	ok = true
    end
end
if not ok then
   err("identity was not added to ssh-agent")
end

-- * (ok) commit with passworded key (provide password explicitly, although will be not necessary as SSH-agent is used)
addfile("some_file21", "test")
check(mtn("ci", "--key", "test_pass@tester.net", "--message", "commit msg"), 0, false, false, "pass\n")

-- * (ok) commit with passworded key (provide no password at all, because SSH-agent is used)
addfile("some_file22", "test")
check(mtn("ci", "--key", "test_pass@tester.net", "--message", "commit msg"), 0, false, false)

