--
-- test exposing bug in how update handles mtn:execute attributes
--
skip_if(ostype=="Windows")

mtn_setup()

writefile("foo", "some data")
check(mtn("add", "foo"), 0, false, false)
check(mtn("attr", "set", "foo", "mtn:execute", "true"), 0, false, false)
check({"test", "-x","foo"}, 0, false, false)
commit()
check({"test", "-x","foo"}, 0, false, false)

writefile("foo", "some more data")
check({"test", "-x","foo"}, 0, false, false)
commit()
check({"test", "-x","foo"}, 0, false, false)

-- the problem here is that update writes the new data to a file in _MTN/tmp and
-- then rename's it on top of the existing file without ever looking to see if
-- the file has mtn:execute set

-- this used to work because all of the various workspace operations called
-- update_any_attrs to reset permissions shotgun style. this was changed prior
-- to 0.43 and permissions are now handled through the editable_tree interface.

check(mtn("update", "-r", "p:"), 0, false, false)
xfail({"test", "-x","foo"}, 0, false, false)

check(mtn("update"), 0, false, false)
xfail({"test", "-x","foo"}, 0, false, false)
