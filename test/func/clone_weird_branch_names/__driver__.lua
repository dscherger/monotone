skip_if(ostype == "Windows") -- file: not supported on native Win32

mtn_setup()

addfile("foo", "blah blah")
commit("my-branch[1,2]-1^3")

copy("test.db", "test-clone.db")

-- some of the special chars need to get double-escaped to get "through"
test_uri="file://" .. url_encode_path(test.root .. "/test-clone.db") ..
  "?" .. url_encode_query("my-branch\\\[1,2\\\]-1^3")
check(nodb_mtn("clone", test_uri), 0, false, false)
check(exists("my-branch[1,2]-1^3"))
check(readfile("foo") == readfile("my-branch[1,2]-1^3/foo"))

