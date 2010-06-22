skip_if(ostype == "Windows") -- file: not supported on native Win32

mtn_setup()

addfile("foo", "blah blah")
commit("mybranch")

copy("test.db", "test-clone.db")
testURI="file://" .. test.root .. "/test-clone.db?mybranch*"

check(nodb_mtn("clone", testURI), 1, false, true)
check(qgrep("you must specify an unambiguous branch to clone", "stderr"))

-- the branch option is invalid in non-URI mode
check(nodb_mtn("clone", "some-server", "mybranch", "--branch=mybranch"), 1, false, true)
check(qgrep("the --branch option is only valid with an URI to clone", "stderr"))

-- finally, this should succeed
check(nodb_mtn("clone", testURI, "--branch=mybranch"), 0, false, false)
check(exists("mybranch"))
check(readfile("foo") == readfile("mybranch/foo"))

