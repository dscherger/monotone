
mtn_setup()

get("inventory_hooks.lua")

addfile("missing", "missing")
addfile("dropped", "dropped")
addfile("original", "original")
addfile("unchanged", "unchanged")
addfile("patched", "patched")
commit()

-- single status changes

addfile("added", "added")
writefile("unknown", "unknown")
writefile("ignored~", "ignored~")

remove("missing")
remove("dropped")
rename("original", "renamed")
writefile("patched", "something has changed")

check(mtn("add", "added"), 0, false, false)
check(mtn("rename", "original", "renamed"), 0, false, false)
check(mtn("drop", "dropped"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
check(grep('^  M 0 0 missing$', "stdout"), 0, false, false)
check(grep('^ A  0 0 added$', "stdout"), 0, false, false)
check(grep('^D   0 0 dropped$', "stdout"), 0, false, false)
check(grep('^R   1 0 original$', "stdout"), 0, false, false)
check(grep('^ R  0 1 renamed$', "stdout"), 0, false, false)
check(grep('^  P 0 0 patched$', "stdout"), 0, false, false)
check(grep('^    0 0 unchanged$', "stdout"), 0, false, false)
check(grep('^  U 0 0 unknown$', "stdout"), 0, false, false)
check(grep('^  I 0 0 ignored~$', "stdout"), 0, false, false)

-- swapped but not moved

check(mtn("revert", "."), 0, false, false)

check(mtn("rename", "unchanged", "temporary"), 0, false, false)
check(mtn("rename", "original", "unchanged"), 0, false, false)
check(mtn("rename", "temporary", "original"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
check(grep('^RRP 1 2 original$', "stdout"), 0, false, false)
check(grep('^RRP 2 1 unchanged$', "stdout"), 0, false, false)

-- swapped and moved

rename("unchanged", "temporary")
rename("original", "unchanged")
rename("temporary", "original")

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
check(grep('^RR  1 2 original$', "stdout"), 0, false, false)
check(grep('^RR  2 1 unchanged$', "stdout"), 0, false, false)

-- rename foo bar; add foo

check(mtn("revert", "."), 0, false, false)

check(mtn("rename", "original", "renamed"), 0, false, false)
check(mtn("add", "original"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
check(grep('^RA  1 0 original$', "stdout"), 0, false, false)
check(grep('^ R  0 1 renamed$', "stdout"), 0, false, false)

-- rotated but not moved
-- - note that things are listed and numbered in path collating order
--   dropped -> missing -> original -> dropped

check(mtn("revert", "."), 0, false, false)

check(mtn("rename", "original", "temporary"), 0, false, false)
check(mtn("rename", "missing", "original"), 0, false, false)
check(mtn("rename", "dropped", "missing"), 0, false, false)
check(mtn("rename", "temporary", "dropped"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
check(grep('^RRP 1 3 dropped$', "stdout"), 0, false, false)
check(grep('^RRP 2 1 missing$', "stdout"), 0, false, false)
check(grep('^RRP 3 2 original$', "stdout"), 0, false, false)

-- rotated and moved

rename("original", "temporary")
rename("missing", "original")
rename("dropped", "missing")
rename("temporary", "dropped")

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
check(grep('^RR  1 3 dropped$', "stdout"), 0, false, false)
check(grep('^RR  2 1 missing$', "stdout"), 0, false, false)
check(grep('^RR  3 2 original$', "stdout"), 0, false, false)

-- dropped but not removed and thus unknown

check(mtn("revert", "."), 0, false, false)

check(mtn("drop", "dropped"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
check(grep('^D U 0 0 dropped$', "stdout"), 0, false, false)

-- added but removed and thus missing

check(mtn("revert", "."), 0, false, false)

check(mtn("add", "added"), 0, false, false)
remove("added")

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
check(grep('^ AM 0 0 added$', "stdout"), 0, false, false)

-- renamed but not moved and thus unknown source and  missing target

check(mtn("revert", "."), 0, false, false)

remove("renamed")
check(mtn("rename", "original", "renamed"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
check(qgrep('^R U 1 0 original$', "stdout"))
check(qgrep('^ RM 0 1 renamed$', "stdout"))

-- moved but not renamed and thus missing source and unknown target

check(mtn("revert", "."), 0, false, false)

rename("original", "renamed")

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
check(grep('^  M 0 0 original$', "stdout"), 0, false, false)
check(grep('^  U 0 0 renamed$', "stdout"), 0, false, false)

-- renamed and patched

check(mtn("revert", "."), 0, false, false)

writefile("renamed", "renamed and patched")
remove("original")

check(mtn("rename", "original", "renamed"), 0, false, false)
check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

check(grep('^R   1 0 original$', "stdout"), 0, false, false)
check(grep('^ RP 0 1 renamed$', "stdout"), 0, false, false)

-- need tests for deleted and renamed directories, once these actually work!
