mtn_setup()

-- the sleep calls here are to ensure a change is visible even if
-- the resolution of mtime is one second or more

time1 = mtime("_MTN/options")

-- commit doesn't change mtime

sleep(1)
writefile("foo", "foo")
check(mtn("add", "foo"), 0, false, false)
commit()
check(time1 == mtime("_MTN/options"))

r1 = base_revision()

-- commit doesn't change mtime

sleep(1)
writefile("foo", "bar")
commit()
check(time1 == mtime("_MTN/options"))

r2 = base_revision()

-- status doesn't change mtime

sleep(1)
check(mtn("status"), 0, false, false)
check(time1 == mtime("_MTN/options"))

-- update on same branch doesn't change mtime

sleep(1)
check(mtn("update", "--revision", r1), 0, false, false)
check(time1 == mtime("_MTN/options"))

-- changing branch updates mtime of cached options

sleep(1)
writefile("foo", "baz")
check(mtn("commit", "--branch", "baz", "--message", "baz"), 0, false, false)
time2 = mtime("_MTN/options")
check(time1 < time2)

r3 = base_revision()

-- update to different branch changes mtime

sleep(1)
check(mtn("update", "--revision", r1), 0, false, false)
time3 = mtime("_MTN/options")
check(time2 < time3)

-- update to same branch doesn't change mtime

sleep(1)
check(mtn("update", "--revision", r2), 0, false, false)
check(time3 == mtime("_MTN/options"))

-- update to different branch changes mtime

sleep(1)
check(mtn("update", "--revision", r3), 0, false, false)
time4 = mtime("_MTN/options")
check(time3 < time4)
