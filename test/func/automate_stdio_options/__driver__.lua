includecommon("automate_stdio.lua")

mtn_setup()

addfile("file", "contents")
commit("testbranch")
writefile("file", "modified")

diffcmd = "o1:r12:h:testbranche l12:content_diffe"
diff1 = run_stdio(string.rep(diffcmd, 2), 0, 0)
diff2 = run_stdio(string.rep(diffcmd, 2), 0, 1)
check(diff1 == diff2)
