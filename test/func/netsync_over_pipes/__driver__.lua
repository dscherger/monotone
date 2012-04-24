skip_if(ostype == "Windows") -- file: not supported on native Win32

mtn_setup()

copy("test.db", "test2.db")

addfile("testfile", "foo")
commit()

test_uri="file://" .. url_encode_path(test.root .. "/test2.db") .. "?testbranch"
check(mtn("sync", test_uri), 0, false, true)
check(not qgrep("error", "stderr"))
check_same_db_contents("test.db", "test2.db")
