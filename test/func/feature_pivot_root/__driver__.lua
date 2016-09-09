mtn_setup()

mkdir("workspace")
check(indir("workspace", mtn("setup", ".", "-b", "testbranch")), 0, false, false)

mkdir("workspace/dir")
mkdir("workspace/dir/subdir")
writefile("workspace/testfile1")

check(indir("workspace", mtn("add", "-R", ".")), 0, false, false)

-- dummy feature should be okay to send and commit
check(indir("workspace", mtn("attr", "set", "",
          "mtn:features", "dummy-feature-for-testing")), 0, false, false)
check(indir("workspace", mtn("commit", "-m", "initial commit")), 0, false, false)

-- do a pivot root
check(indir("workspace", mtn("pivot_root", "dir/subdir", "old_root")), 0, false, false)
check(exists("workspace/old_root/testfile1"))

-- check if the attribute is on the root node, again, and..
check(indir("workspace", mtn("attr", "get", "", "mtn:features")), 0, true, false)
check(qgrep(" : mtn:features=dummy-feature-for-testing", "stdout"))

-- .. not on the old one
check(indir("workspace", mtn("attr", "get", "old_root", "mtn:features")), 0, true, false)
check(qgrep("no attribute 'mtn:features' on path 'old_root'", "stdout"))
