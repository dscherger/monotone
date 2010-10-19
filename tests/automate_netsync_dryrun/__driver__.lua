-- Test automate sync/pull/push --dry-run
--
-- Same structure as non-dry-run test, but all executions are --dry-run
-- (ediff-directories "../automate_netsync" "../automate_netsync_dry-run")

include("/common/netsync.lua")
include("/common/automate_stdio.lua")

mtn_setup()
netsync.setup()

-- Create a base revision, in test.db branch 'foo'
-- Override date so we can compare it below
function mtn_date(...)
   return mtn("--date=2010-09-01T12:00:00", ...)
end
   
addfile("testfile", "blah stuff")
commit("foo", "R1", mtn_date)
R1 = base_revision()

-- Serve test.db
srv = netsync.start()

-- Pull branch 'bar' from test.db to test2.db; nothing to transfer, so
-- all zeros on stdout. stderr has progress messages, ticker
check(mtn2("automate", "pull", "--dry-run", "mtn://" .. srv.address .. "?bar"), 0, true, false)
check(readfile("stdout") == "receive_revision \"0\"\n    receive_cert \"0\"\n     receive_key \"0\"\n")

-- Pull branch 'foo', confirm dryrun output
get("pull_r1.expected")
check(mtn2("automate", "pull", "--dry-run", "mtn://" .. srv.address .. "?foo"), 0, true, false)
canonicalize("stdout")
check(samefile("pull_r1.expected", "stdout"))
-- stderr has ticker; we don't check that here

-- check that test.db does _not_ have R1, due to --dry-run
check(mtn2("automate", "get_revision", R1), 1, nil, true)
check(qgrep("no revision 114f6aa58c7707bf83516d4080ca6268c36640ad found in database", "stderr"))

-- Push a cert from test2.db to test.db; test 2
-- We need a revision to put a cert on, so pull R1 for real
check(mtn2("automate", "pull", "mtn://" .. srv.address .. "?foo"), 0, false, false)

get("push_cert.expected")
check(mtn2("automate", "cert", R1, "test", "value"), 0, nil, nil)
check(mtn2("automate", "push", "--dry-run", "mtn://" .. srv.address .. "?foo"), 0, true, false)
canonicalize("stdout")
check(samefile("push_cert.expected", "stdout"))

srv:stop()

-- check that the test cert did _not_ get transfered
check(mtn("au", "certs", R1), 0, true, nil)
check(not (qgrep("name \"test\"", "stdout")))

-- Test 'automate stdio sync --dry-run' output and ticker

-- New file on a new branch in test2.db; tests 1
-- Test ticker output; no ticks for dryrun
writefile("testfile2", "blah foo")
check(mtn2("add", "testfile2"), 0, nil, false)
check(mtn2("commit", "--branch=foo2", "--message=R2", "--date=2010-09-01T12:00:00"), 0, nil, false)
R2 = base_revision()

srv = netsync.start()
local cmd = make_stdio_cmd({"sync", "mtn://" .. srv.address .. "?foo*"}, {{"dry-run", ""}})

check(mtn2("automate", "stdio"), 0, true, false, cmd)
output  = parse_stdio(readfile("stdout"), 0, 0, "m")
tickers = parse_stdio(readfile("stdout"), 0, 0, "t")

get("send_branch.expected")
check(readfile("send_branch.expected") == output)

function any_of(tbl, val)
    for _,v in pairs(tbl) do
        if string.find(v, val) then
            return true
        end
    end
    return false
end

check(any_of(tickers, "c:certificates;k:keys;r:revisions;"))
check(any_of(tickers, "c;k;r;"))

check(any_of(tickers, ">:bytes in"))
check(not any_of(tickers, "<:bytes out"))
check(not any_of(tickers, "r:revs in"))
check(not any_of(tickers, "c:certs in"))
check(not any_of(tickers, "C:certs out"))

check(not any_of(tickers, "R#1"))
check(not any_of(tickers, "r#1"))
check(not any_of(tickers, "C#1"))
check(not any_of(tickers, "c#1"))

srv:stop()

-- push and pull a key; test 3 and 6. Note that keys are not sent
-- unless they are used to sign something; we sign another test cert;
-- tests 5. Can't use 'genkey'; that gives a random key signature
getstd("common/john_key.packet", "john_key.packet")
getstd("common/jane_key.packet", "jane_key.packet")
check(mtn("read", "john_key.packet"), 0, nil, false)
check(mtn2("read", "jane_key.packet"), 0, nil, false)

check(mtn("--key=john@doe.com", "cert", R1, "test", "value2"), 0, nil, nil)
check(mtn2("--key=jane@doe.com", "cert", R2, "test", "value2"), 0, nil, nil)

srv = netsync.start()
local cmd = make_stdio_cmd({"sync", "mtn://" .. srv.address .. "?*"}, {{"dry-run", ""}})
check(mtn2("automate", "stdio"), 0, true, false, cmd)
get("sync_keys.expected")

output = parse_stdio(readfile("stdout"), 0, 0, "m")

check(readfile("sync_keys.expected") == output)

-- end of file
