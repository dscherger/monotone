
mtn_setup()

-- since we run with LANG=C, we check for dates like Mon Nov 23 12:34:04 2009
local prefix_pattern = "%[%a+ %a+ %d%d %d%d:%d%d:%d%d %d%d%d%d%] "

check(mtn("crash", "N", "--timestamps"), 1, false, true)
check(string.find(readfile("stderr"), prefix_pattern .. "mtn: misuse: There is no spoon.") ~= nil)

check(mtn("crash", "E", "--timestamps"), 1, false, true)
check(string.find(readfile("stderr"), prefix_pattern .. "mtn: error: There is no spoon.") ~= nil)

