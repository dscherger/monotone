-- this is an accompanying test for #30150

mtn_setup()
check(get("date_untrusted.lua"))
check(get("author_untrusted.lua"))
check(get("all_untrusted.lua"))

addfile("foo", "blablabla")
commit()
rev=base_revision()
shortrev=string.sub(rev, 1, 8)..".."

check(mtn("annotate", "foo", "--rcfile", "date_untrusted.lua"), 0, true, false)
check(qgrep("^" .. shortrev .. " by tester: ", "stdout"))

check(mtn("annotate", "foo", "--rcfile", "author_untrusted.lua"), 0, true, false)
check(qgrep("^" .. shortrev .. " [0-9]{4}(-[0-9]{2}){2}T[0-9]{2}(:[0-9]{2}){2}: ", "stdout"))

check(mtn("annotate", "foo", "--rcfile", "all_untrusted.lua"), 0, true, false)
check(qgrep("^" .. shortrev .. ": ", "stdout"))
