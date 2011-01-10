mtn_setup()

r = {}

addfile("foo", "0")
commit()
table.insert(r, 1, base_revision())

for i=2,10 do
    writefile("foo", i)
    commit()
    table.insert(r, i, base_revision())
end

check(mtn("log", "--brief", "--no-graph", "--from", r[7], "--to", r[4]), 0, true, false)

check(not qgrep(r[1], "stdout"))
check(not qgrep(r[2], "stdout"))
check(not qgrep(r[3], "stdout"))
check(not qgrep(r[4], "stdout"))
check(qgrep(r[5], "stdout"))
check(qgrep(r[6], "stdout"))
check(qgrep(r[7], "stdout"))
check(not qgrep(r[8], "stdout"))
check(not qgrep(r[9], "stdout"))
check(not qgrep(r[10], "stdout"))

check(mtn("log", "--brief", "--no-graph", "-r", r[3], "-r", r[4], "-r", r[5]), 0, true, false)

check(not qgrep(r[1], "stdout"))
check(not qgrep(r[2], "stdout"))
check(qgrep(r[3], "stdout"))
check(qgrep(r[4], "stdout"))
check(qgrep(r[5], "stdout"))
check(not qgrep(r[6], "stdout"))
check(not qgrep(r[7], "stdout"))
check(not qgrep(r[8], "stdout"))
check(not qgrep(r[9], "stdout"))
check(not qgrep(r[10], "stdout"))

check(mtn("log", "--brief", "--no-graph", "-r", r[3], "-r", r[4], "-r", r[5], "--from", r[4]), 0, true, false)

check(not qgrep(r[1], "stdout"))
check(not qgrep(r[2], "stdout"))
check(qgrep(r[3], "stdout"))
check(qgrep(r[4], "stdout"))
check(not qgrep(r[5], "stdout"))
check(not qgrep(r[6], "stdout"))
check(not qgrep(r[7], "stdout"))
check(not qgrep(r[8], "stdout"))
check(not qgrep(r[9], "stdout"))
check(not qgrep(r[10], "stdout"))
