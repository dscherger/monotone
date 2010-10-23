skip_if(ostype == "Windows") -- file: not supported on native Win32

mtn_setup()

copy("test.db", "test2.db")

addfile("testfile", "foo")
commit()

local cwd = chdir(".").."/"
check(mtn("sync", "file:///"..cwd.."test2.db?testbranch"), 0, false, true)
check(not qgrep("error", "stderr"))
check_same_db_contents("test.db", "test2.db")
