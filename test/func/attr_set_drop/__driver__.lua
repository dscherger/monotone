-- attr_drop mtn:execute does not clear execute bits

skip_if(ostype=="Windows")
skip_if(string.sub(ostype, 1, 6)=="CYGWIN")-- test -x broken

mtn_setup()

writefile("foo", "some data")
check(mtn("add", "foo"), 0, false, false)

-- check that no execute bits are set
check({"test", "!", "-x","foo"}, 0, false, false)

-- setting mtn:execute sets the file's execute bits
check(mtn("attr", "set", "foo", "mtn:execute", "true"), 0, false, false)
check({"test", "-x","foo"}, 0, false, false)

-- dropping mtn:execute clears the file's execute bits
check(mtn("attr", "drop", "foo", "mtn:execute"), 0, false, false)
check({"test", "!", "-x","foo"}, 0, false, false)
