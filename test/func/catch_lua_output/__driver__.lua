
mtn_setup()

check(get("hooks.lua"))

check(mtn("automate", "select", "*", "--rcfile", "hooks.lua"), 0, true, true)

check(qgrep("lua: this is catched", "stderr"))
check(qgrep("lua: this is also catched", "stderr"))

check(qgrep("lua: line breaks", "stderr"))
check(qgrep("lua: are handled", "stderr"))
check(qgrep("lua: properly as well", "stderr"))

check(qgrep("this is not catched", "stdout"))
check(not qgrep("lua: this is not catched", "stderr"))

check(qgrep("this is also not catched", "stderr"))
check(not qgrep("lua: this is also not catched", "stderr"))

-- load a slightly different version with no regular stdout output
-- which would confuse the stdio parser
check(get("hooks_automate.lua"))

check(mtn("automate", "stdio", "--rcfile", "hooks_automate.lua"), 0, true, true, "l6:select1:*e")
check(qgrep("this is also not catched", "stderr"))

includecommon("automate_stdio.lua")
progress = parse_stdio(readfile("stdout"), 0, nil, 'p')

check(progress[1] == "lua: this is catched")
check(progress[2] == "lua: this is also catched")
check(progress[3] == "lua: line breaks\nlua: are handled\nlua: properly as well")

