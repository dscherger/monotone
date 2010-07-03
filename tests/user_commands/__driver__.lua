mtn_setup()

check(get("extra_rc"))

addfile("foo", "random info\n")
commit()
rev_a = base_revision()

-- a simple command w/o the use of options
check(mtn("check_head", "--rcfile=extra_rc", rev_a), 0, false, true)
check(samelines("stderr", {"mtn: lua: heads are equal", "mtn: lua: end of command"}))

addfile("bar", "more random info\n")
commit()
rev_b = base_revision()

-- another simple command this time with arguments which are set as options
-- in the underlying automate content_diff calls. we test here that
--  a) the basic call succeeds
--  b) options given to mtn_automate are parsed correctly (see extra_rc)
--  c) outer command line arguments are not passed to the inner mtn_automate
--     calls (otherwise both revisions would lead to path restriction errors)
check(mtn("diff_two_revs", "--rcfile=extra_rc", rev_a, rev_b), 0, false, false)


