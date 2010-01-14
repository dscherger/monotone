
include("/common/netsync.lua")
include("/common/automate_stdio.lua")

mtn_setup()
netsync.setup()

addfile("testfile", "blah stuff")
commit("foo")
R1 = base_revision()

srv = netsync.start()

check(mtn2("automate", "pull", srv.address, "bar"), 0, false, false)
check(mtn2("automate", "get_revision", R1), 1, false, false)

check(mtn2("automate", "pull", srv.address, "foo"), 0, false, false)
check(mtn2("automate", "cert", R1, "test", "value"), 0, false, false)
check(mtn2("automate", "push", srv.address, "foo"), 0, false, false)

srv:stop()

check_same_stdout(
    mtn("au", "get_revision", R1),
    mtn2("au", "get_revision", R1)
)

check(mtn("au", "certs", R1), 0, true, false)
check(qgrep('name "test"', "stdout"))

addfile("testfile2", "blah foo")
commit("foo2")
R2 = base_revision()

srv = netsync.start()
local srvaddr = string.len(srv.address) .. ":" .. srv.address
local cmd = "l4:sync"..srvaddr.."4:foo*e"

check(mtn2("automate", "stdio"), 0, true, false, cmd)
tickers = parse_stdio(readfile("stdout"), 0, 0, "t")

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
