skip_if(ostype=="Windows")
skip_if(string.sub(ostype, 1, 6)=="CYGWIN") -- test -x broken
skip_if(not existsonpath("chmod"))

mtn_setup()

writefile("foo", "some data")
writefile("bar", "other data")

check(mtn("add", "foo", "bar"), 0, false, false)
check(mtn("attr", "set", "foo", "mtn:execute", "true"), 0, false, false)

check({"test", "-x","foo"}, 0, false, false)
check({"test", "!", "-x","bar"}, 0, false, false)

commit()

-- now flip the attributes so that foo has a dormant mtn:execute

check(mtn("attr", "drop", "foo", "mtn:execute"), 0, false, false)
check(mtn("attr", "set", "bar", "mtn:execute", "true"), 0, false, false)

check({"test", "!", "-x","foo"}, 0, false, false)
check({"test", "-x","bar"}, 0, false, false)

commit()

-- set execute on foo and clear on bar

check({"chmod", "+x", "foo"}, 0, false, false)
check({"chmod", "-x", "bar"}, 0, false, false)

check({"test", "-x","foo"}, 0, false, false)
check({"test", "!", "-x","bar"}, 0, false, false)

-- revert foo clears the execute bits

check(mtn("revert", "foo"), 0, false, false)
check({"test", "!", "-x","foo"}, 0, false, false)
check({"test", "!", "-x","bar"}, 0, false, false)

-- revert bar sets the execute bits

check(mtn("revert", "bar"), 0, false, false)
check({"test", "!", "-x","foo"}, 0, false, false)
check({"test", "-x","bar"}, 0, false, false)
