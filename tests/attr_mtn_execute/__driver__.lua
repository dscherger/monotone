--
-- test setting/clearing of the the execute file attribute works
--
skip_if(ostype=="Windows")

mtn_setup()

writefile("foo", "some data")
check(mtn("add", "foo"), 0, false, false)
commit()
without_x = base_revision()

check(mtn("attr", "set", "foo", "mtn:execute", "true"), 0, false, false)
check({"test", "-x","foo"}, 0, false, false)
commit()
with_x = base_revision()

check(mtn("update", "-r", without_x), 0, true, true)
check({"test", "!", "-x","foo"}, 0, false, false)

check(mtn("update", "-r", with_x), 0, false, false)
check({"test", "-x","foo"}, 0, false, false)
