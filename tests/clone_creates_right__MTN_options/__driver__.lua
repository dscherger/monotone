skip_if(ostype == "Windows") -- file: not supported on native Win32

mtn_setup()

addfile("testfile", "foo")
commit()
rev = base_revision()

copy("test.db", "test-clone.db")
writefile("testfile", "blah")
commit()

testURI="file://" .. test.root .. "/test-clone.db?testbranch"

-- We use RAW_MTN because it used to be that passing --db= (as
-- MTN does) would hide a bug in this functionality...

-- all of these inherit options settings from the current _MTN dir
-- unless they override them on the command line

check(nodb_mtn("clone", testURI, "test_dir1"), 0, false, false)
check(nodb_mtn("clone", "--revision", rev, testURI, "test_dir2"), 0, false, false)
check(nodb_mtn("--db=" .. test.root .. "/test-new.db", "clone", testURI, "test_dir3"), 0, false, false)
check(nodb_mtn("--db=" .. test.root .. "/test-new.db", "clone", testURI, "--revision", rev, "test_dir4"), 0, false, false)

-- checkout fails if the specified revision is not a member of the specified branch
testURI="file://" .. test.root .. "/test-clone.db?foobar"
check(nodb_mtn("clone", testURI, "--revision", rev, "test_dir5"), 1, false, false)
check(nodb_mtn("cert", rev, "branch", "foobar", "-d", "test-clone.db"), 0, false, false)
check(nodb_mtn("clone", testURI, "--revision", rev, "test_dir6"), 0, false, false)


for i = 1,2 do
  local dir = "test_dir"..i
  L("dir = ", dir, "\n")
  check(exists(dir.."/_MTN/options"))
  check(qgrep("default.mtn", dir.."/_MTN/options"))
  check(qgrep("testbranch", dir.."/_MTN/options"))
end

for i = 3,4 do
  local dir = "test_dir"..i
  L("dir = ", dir, "\n")
  check(exists(dir.."/_MTN/options"))
  check(qgrep("test-new.db", dir.."/_MTN/options"))
  check(qgrep("testbranch", dir.."/_MTN/options"))
end

check(qgrep("foobar", "test_dir6/_MTN/options"))
