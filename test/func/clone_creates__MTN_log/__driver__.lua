skip_if(ostype == "Windows") -- file: not supported on native Win32

mtn_setup()

check(get("commit_log.lua"))

writefile("_MTN/log", "Log entry")

writefile("input.txt", "version 0 of the file")

check(mtn("add", "input.txt"), 0, false, false)

check(mtn("--branch=testbranch", "--rcfile=commit_log.lua", "commit"), 0, false, false)

copy("test.db", "test-clone.db")

test_uri="file://" .. url_encode_path(test.root .. "/test-clone.db") .. "?testbranch"
check(nodb_mtn("clone", test_uri, "testbranch"), 0, false, true)

check(exists("testbranch/_MTN/log"))
check(fsize("_MTN/log") == 0)
check(fsize("testbranch/_MTN/log") == 0)
