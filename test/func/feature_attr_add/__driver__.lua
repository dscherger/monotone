mtn_setup()

addfile("testfile1", "foo")

-- unknown feature 'foo' should yield an error
check(mtn("attr", "set", "", "mtn:features", "foo"), 1, false, false)

-- dummy feature should be okay to send and commit
check(mtn("attr", "set", "",
          "mtn:features", "dummy-feature-for-testing"), 0, false, false)
commit()

-- trying to set the attribute on a non-root node should yield an
-- error.
check(mtn("attr", "set", "testfile1",
          "mtn:features", "dummy-feature-for-testing"), 1, false, false)
