skip_if(ostype=="Windows")
skip_if(not existsonpath("chmod"))

mtn_setup()

writefile("foo", "some data")

check(mtn("add", "foo"), 0, false, false)
check(mtn("attr", "set", "foo", "mtn:execute", "true"), 0, false, false)

-- setting the execute attr should set execute permission
check({"test", "-x","foo"}, 0, false, false)

-- revert the execute attr should clear execute permissions
check(mtn("revert", "foo"), 0, false, false)
xfail({"test", "!", "-x","foo"}, 0, false, false)
