
-- This test checks that 'mtn log' of deleted and renamed files shows
-- nothing in a further revision, as they are not applicable any more.

mtn_setup()

addfile("foo", "foo")
commit("testbranch", "Addition of foo.")

addfile("bar", "bar")
commit("testbranch", "Addition of bar.")

addfile("baz", "baz")
commit("testbranch", "Addition of baz.")

check(mtn("drop", "bar", "baz"), 0, false, false)
commit()
remove("bar")
remove("baz")

rename("foo", "bar")
check(mtn("rename", "foo", "bar"), 0, false, false)
commit()

check(mtn("log", "foo"), 1, 0, false)

check(mtn("log", "baz"), 1, 0, false)

check(mtn("log", "bar"), 0, true, false)
rename("stdout", "log")
check(grep("^Addition of [a-z][a-z][a-z].$", "log"), 0, true)
getfile("first")
check(samefile("stdout", "first"))

check(mtn("log"), 0, true, false)
rename("stdout", "log")
check(grep("^Addition of [a-z][a-z][a-z].$", "log"), 0, true)
getfile("second")
check(samefile("stdout", "second"))
