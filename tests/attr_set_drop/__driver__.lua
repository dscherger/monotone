-- attr_drop mtn:execute does not clear execute bits

skip_if(ostype=="Windows")

mtn_setup()

writefile("foo", "some data")
check(mtn("add", "foo"), 0, false, false)

-- check that no execute bits are set
check({"test", "!", "-x","foo"}, 0, false, false)

-- setting mtn:execute does set the file's execute bits
check(mtn("attr", "set", "foo", "mtn:execute", "true"), 0, false, false)
check({"test", "-x","foo"}, 0, false, false)

-- dropping mtn:execute does NOT clear the file's execute bits
-- this is a minor bug/inconsistency since set does set the execute bits
-- it's caused by the fact that both attr_set and attr_drop call
-- updade_any_attrs which only sets currently existing attrs and doesn't clear
-- them
check(mtn("attr", "drop", "foo", "mtn:execute"), 0, false, false)
xfail({"test", "!", "-x","foo"}, 0, false, false)
