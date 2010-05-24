skip_if(ostype == "Windows") -- file: not supported on native Win32

mtn_setup()

addfile("testfile", "foo")
commit()

copy("test.db", "test-clone.db")
testURI="file:" .. test.root .. "/test-clone.db"

check(nodb_mtn("clone", testURI, "testbranch", "test_dir1"), 0, false, false)

writefile("test_dir2")
check(nodb_mtn("clone", testURI, "testbranch", "test_dir2"), 1, false, false)

mkdir("test_dir3")
check(nodb_mtn("clone", testURI, "testbranch", "test_dir3"), 1, false, false)

if existsonpath("chmod") and existsonpath("test") then
  -- skip this part if run as root (hi Gentoo!)
  -- Also skip if on Windows and Cygwin, since these permissions are not enforced there
  if check({"test", "-O", "/"}, false, false, false) == 0 or
     ostype == "Windows" or
     string.find (ostype, "CYGWIN")
  then
    partial_skip = true
  else
    mkdir("test_dir4")
    check({"chmod", "444", "test_dir4"}, 0, false)
    check(nodb_mtn("clone", testURI, "testbranch", "test_dir4"),
             1, false, false)
    check(nodb_mtn("clone", testURI, "testbranch", "test_dir4/subdir"),
             1, false, false)
    -- Reset the permissions so Autotest can correctly clean up our
    -- temporary directory.
    check({"chmod", "700", "test_dir4"}, 0, false)
  end
else
  partial_skip = true
end
