--  Test automate sync/pull/push

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

-- Pull branch 'bar' from test.db to test2.db; nothing transferred, so
-- no output on stdout; stderr has progress messages, ticker
check(mtn2("automate", "pull", "mtn://" .. srv.address .. "?bar"), 0, nil, false)

-- Fails because R1 is on branch 'foo'
check(mtn2("automate", "get_revision", R1), 1, nil, true)
check(qgrep("no revision 114f6aa58c7707bf83516d4080ca6268c36640ad found in database", "stderr"))

-- Pull branch 'foo', confirm output
get("pull_r1.expected")
check(mtn2("automate", "pull", "mtn://" .. srv.address .. "?foo"), 0, true, false)
canonicalize("stdout")
check(samefile("pull_r1.expected", "stdout"))
-- stderr has ticker; we don't check that here

-- Push a cert from test2.db to test.db
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

-- New file on a new branch in test.db
addfile("testfile2", "blah foo")
commit("foo2", "R2", mtn_date)
R2 = base_revision()

srv = netsync.start()
local cmd = make_stdio_cmd({"sync", "mtn://" .. srv.address .. "?foo*"})

get("sync.expected")
check(mtn2("automate", "stdio"), 0, true, false, cmd)
output  = parse_stdio(readfile("stdout"), 0, 0, "m")
tickers = parse_stdio(readfile("stdout"), 0, 0, "t")

check(readfile("sync.expected") == output)

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

check(not any_of(tickers, "R#1"))
check(any_of(tickers, "r#1"))
check(not any_of(tickers, "C#1"))
check(any_of(tickers, "c#4"))

check(any_of(tickers, "<;>;C;R;c;r;"))

srv:stop()

-- FIXME: check output format for key transfer

-- end of file
