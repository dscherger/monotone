--
-- test setting/clearing of the the execute file attribute works
--
skip_if(ostype=="Windows")

mtn_setup()

writefile("foo", "some data")
check(mtn("add", "foo"), 0, false, false)
commit()
rev1 = base_revision()

check(mtn("attr", "set", "foo", "mtn:execute", "true"), 0, false, false)
check({"test", "-x","foo"}, 0, false, false)
commit()
rev2 = base_revision()

writefile("bar", "bar data")
check(mtn("add", "bar"), 0, false, false)
commit()
rev3 = base_revision()

check(mtn("update", "-r", rev1), 0, false, false)
check({"test", "!", "-x","foo"}, 0, false, false)

writefile("baz", "some data")
check(mtn("add", "baz"), 0, false, false)
commit()
rev4 = base_revision()

-- pluck the revision that set foo's execute bits
check(mtn("pluck", "-r", rev2), 0, false, false)
check({"test", "-x","foo"}, 0, false, false)
