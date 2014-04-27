-- Test automate sync/pull/push
--
-- Automate output is done by shared code; there are six formats to test:
-- 1) send revision with certs
-- 2) send certs on existing revision
-- 3) send key
-- 4) receive revision with certs
-- 5) receive certs on existing revision
-- 6) receive key

includecommon("netsync.lua")
includecommon("automate_stdio.lua")

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

-- Pull branch 'bar' from test.db to test2.db; nothing transferred, so
-- no output on stdout; stderr has progress messages, ticker
check(mtn2("automate", "pull", "mtn://" .. srv.address .. "?bar"), 0, nil, false)

-- Fails because R1 is on branch 'foo'
check(mtn2("automate", "get_revision", R1), 1, nil, true)
check(qgrep("no revision 114f6aa58c7707bf83516d4080ca6268c36640ad found in database", "stderr"))

-- Pull branch 'foo', confirm output; tests 4
get("pull_r1.expected")
check(mtn2("automate", "pull", "mtn://" .. srv.address .. "?foo"), 0, true, false)
canonicalize("stdout")
check(samefile("pull_r1.expected", "stdout"))
-- stderr has ticker; we don't check that here

-- Push a cert from test2.db to test.db; test 2
get("push_cert.expected")
check(mtn2("automate", "cert", R1, "test", "value"), 0, nil, nil)
check(mtn2("automate", "push", "mtn://" .. srv.address .. "?foo"), 0, true, false)
canonicalize("stdout")
check(samefile("push_cert.expected", "stdout"))

srv:stop()

-- check that test.db and test2.db have the same data for R1
check_same_stdout(
    mtn("au", "get_revision", R1),
    mtn2("au", "get_revision", R1)
)

-- check that the test cert got transfered
check(mtn("au", "certs", R1), 0, true, false)
check(qgrep('name "test"', "stdout"))

-- Test 'automate stdio sync' output and ticker

-- New file on a new branch in test2.db; tests 1
writefile("testfile2", "blah foo")
check(mtn2("add", "testfile2"), 0, nil, false)
check(mtn2("commit", "--branch=foo2", "--message=R2", "--date=2010-09-01T12:00:00"), 0, nil, false)
R2 = base_revision()

srv = netsync.start()
local cmd = make_stdio_cmd({"sync", "mtn://" .. srv.address .. "?foo*"})

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
check(any_of(tickers, "<:bytes out"))
check(any_of(tickers, "r:revs in"))
check(any_of(tickers, "R:revs out"))
check(any_of(tickers, "c:certs in"))
check(any_of(tickers, "C:certs out"))

check(any_of(tickers, "R#1"))
check(not any_of(tickers, "r#1"))
check(any_of(tickers, "C#4"))
check(not any_of(tickers, "c#1"))

check(any_of(tickers, "<;>;C;R;c;r;"))

srv:stop()

-- push and pull a key; test 3 and 6. Note that keys are not sent
-- unless they are used to sign something; we sign another test cert;
-- tests 5. Can't use 'genkey'; that gives a random key signature
getcommon("john_key.packet", "john_key.packet")
getcommon("jane_key.packet", "jane_key.packet")
check(mtn("read", "john_key.packet"), 0, nil, false)
check(mtn2("read", "jane_key.packet"), 0, nil, false)

check(mtn("--key=john@doe.com", "cert", R2, "test", "value"), 0, nil, nil)
check(mtn2("--key=jane@doe.com", "cert", R2, "test", "value"), 0, nil, nil)

srv = netsync.start()
local cmd = make_stdio_cmd({"sync", "mtn://" .. srv.address .. "?*"})
check(mtn2("automate", "stdio"), 0, true, false, cmd)
get("sync_keys.expected")

output = parse_stdio(readfile("stdout"), 0, 0, "m")

check(readfile("sync_keys.expected") == output)

-- end of file
