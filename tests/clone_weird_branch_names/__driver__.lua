skip_if(ostype == "Windows") -- file: not supported on native Win32

mtn_setup()

addfile("foo", "blah blah")
commit("my-branch[1,2]-1^3")

copy("test.db", "test-clone.db")
-- some of the special chars need to get double-escaped to get "through"
testURI="file://" .. test.root .. "/test-clone.db?my-branch\\\[1,2\\\]-1^3"

check(nodb_mtn("clone", testURI), 0, false, false)
check(exists("my-branch[1,2]-1^3"))
check(readfile("foo") == readfile("my-branch[1,2]-1^3/foo"))

