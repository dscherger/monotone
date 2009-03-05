-- attrs re-set on unrelated files

skip_if(ostype=="Windows")
skip_if(not existsonpath("chmod"))

mtn_setup()

writefile("foo", "some data")
writefile("bar", "other data")

check(mtn("add", "foo", "bar"), 0, false, false)

-- check that no execute bit is set
check({"test", "!", "-x","foo"}, 0, false, false)
check({"test", "!", "-x","bar"}, 0, false, false)

-- setting mtn:execute does set the file's execute bits
check(mtn("attr", "set", "foo", "mtn:execute", "true"), 0, false, false)
check({"test", "-x","foo"}, 0, false, false)
check({"test", "!", "-x","bar"}, 0, false, false)

-- manually clear the execute bits from foo
check({"chmod", "-x", "foo"}, 0, false, false)
check({"test", "!", "-x","foo"}, 0, false, false)
check({"test", "!", "-x","bar"}, 0, false, false)

-- now tell monotone to set the execute bits on bar
-- this should not touch foo
check(mtn("attr", "set", "bar", "mtn:execute", "true"), 0, false, false)
check({"test", "!", "-x","foo"}, 0, false, false)
check({"test", "-x","bar"}, 0, false, false)

-- manually clear the execute bits from foo and bar
check({"chmod", "-x", "foo", "bar"}, 0, false, false)
check({"test", "!", "-x","foo"}, 0, false, false)
check({"test", "!", "-x","bar"}, 0, false, false)

-- revert changes to foo
-- this should not touch bar
check(mtn("revert", "foo"), 0, false, false)
check({"test", "!", "-x","foo"}, 0, false, false)
xfail({"test", "!", "-x","bar"}, 0, false, false)
