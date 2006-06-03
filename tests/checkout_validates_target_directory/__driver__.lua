
mtn_setup()

addfile("testfile", "foo")
commit()

check(mtn("--branch=testbranch", "checkout", "test_dir1"),
         0, false, false)

writefile("test_dir2")
check(mtn("--branch=testbranch", "checkout", "test_dir2"),
         1, false, false)

mkdir("test_dir3")
check(mtn("--branch=testbranch", "checkout", "test_dir3"),
         1, false, false)

if existsonpath("chmod") and existsonpath("test") then
  -- skip this part if run as root (hi Gentoo!)
  if check(cmd("test", "-O", "/"), false, false, false) == 0 then
    partial_skip = true
  else
    mkdir("test_dir4")
    check(cmd("chmod", "444", "test_dir4"), 0, false)
    check(mtn("--branch=testbranch", "checkout", "test_dir4"),
             1, false, false)
    check(mtn("--branch=testbranch", "checkout", "test_dir4/subdir"),
             1, false, false)
    -- Reset the permissions so Autotest can correctly clean up our
    -- temporary directory.
    check(cmd("chmod", "700", "test_dir4"), 0, false)
  end
else
  partial_skip = true
end

-- checkout <existing dir> normally is disallowed
-- but as a special case, we allow "checkout ."

mkdir("test_dir5")
chdir("test_dir5")
check(mtn("--branch=testbranch", "checkout", "."), 0, false, false)
chdir("..")
