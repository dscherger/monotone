skip_if(ostype == "Windows") -- file: not supported on native Win32

mtn_setup()

addfile("foo", "blah blah")
commit("mybranch")

copy("test.db", "test-clone.db")
testURI="file://" .. test.root .. "/test-clone.db?mybranch"

check(nodb_mtn("clone", testURI), 0, false, false)
check(exists("mybranch"))
check(readfile("foo") == readfile("mybranch/foo"))

-- but now that that directory exists, this clone should fail
check(nodb_mtn("clone", testURI), 1, false, false)

-- but succeed if given a specific dir
check(nodb_mtn("clone", testURI, "otherdir"), 0, false, false)

-- clone into . should not fail, like checkout
mkdir("test4")
check(indir("test4", nodb_mtn("clone", testURI, ".")), 0, false, false)
