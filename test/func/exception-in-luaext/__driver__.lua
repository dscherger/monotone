mtn_setup()

get("evilrc.lua")

-- so there will be a revision to call our hook function on
addfile("foo", "bar")
commit()

check(mtn("heads", "--rcfile", "evilrc.lua"), 0, false, false)
