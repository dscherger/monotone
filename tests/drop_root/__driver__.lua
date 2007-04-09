
mtn_setup()

-- We should not allow dropping the root, no matter how
-- we refer to it. Test that via "." in the root itself..

xfail(mtn("drop", "."), 1, false, false)

-- ..or from a subdirectory via ".."

mkdir("dir")
check(mtn("add", "dir"), 0, false, false)
xfail(indir("dir", mtn("drop", "..", "--recursive")), 1, false, false)
