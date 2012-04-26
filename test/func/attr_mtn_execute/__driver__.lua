--
-- test setting/clearing of the the execute file attribute works
--
skip_if(ostype=="Windows")
skip_if(string.sub(ostype, 1, 6)=="CYGWIN") -- test -x broken

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

-- test checkout with mtn:execute

check(mtn("checkout", "checkout"), 0, false, false)
check(indir("checkout", {"test", "-x","foo"}, 0, false, false))

-- test clone with mtn:execute

copy("test.db", "test-clone.db")
test_uri="file://" .. url_encode_path(test.root .. "/test-clone.db") .. "?testbranch"

check(nodb_mtn("clone", test_uri, "clone"), 0, false, true)
check(indir("clone", {"test", "-x","foo"}, 0, false, false))
