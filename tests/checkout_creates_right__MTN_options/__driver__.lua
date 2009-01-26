
mtn_setup()

addfile("testfile", "foo")
commit()
rev = base_revision()

-- We use RAW_MTN because it used to be that passing --db= (as
-- MTN does) would hide a bug in this functionality...

check(raw_mtn("--db=test.db", "--branch=testbranch", "checkout", "test_dir1"), 0, false, false)
check(raw_mtn("--db=test.db", "--branch=testbranch", "checkout", "--revision", rev, "test_dir2"), 0, false, false)
check(raw_mtn("--db=test.db", "checkout", "--revision", rev, "test_dir3"), 0, false, false)

-- checkout fails if the specified revision is not a member of the specified branch
check(raw_mtn("--db=test.db", "--branch=foobar", "checkout", "--revision", rev, "test_dir4"), 1, false, false)
check(mtn("cert", rev, "branch", "foobar"), 0, false, false)
check(raw_mtn("--db=test.db", "--branch=foobar", "checkout", "--revision", rev, "test_dir5"), 0, false, false)


for i = 1,3 do
  local dir = "test_dir"..i
  L("dir = ", dir, "\n")
  check(exists(dir.."/_MTN/options"))
  check(qgrep("test.db", dir.."/_MTN/options"))
  check(qgrep("testbranch", dir.."/_MTN/options"))
end

check(qgrep("foobar", "test_dir5/_MTN/options"))
