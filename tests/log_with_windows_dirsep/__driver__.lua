-- Check that log handles Windows directory separators, on Windows
-- closes bug#9269

skip_if(ostype~="Windows")

mtn_setup()

addfile("base", "base")
mkdir("foo")
addfile("foo/bar", "bar")
commit()

check(mtn("log", "foo\\bar"), 0, false, false)

-- end of file
