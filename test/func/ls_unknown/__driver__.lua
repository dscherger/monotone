-- Test various cases of 'ls unknown'

mtn_setup()

mkdir("known")
addfile("known/1", "known 1")
addfile("known/2", "known 2")
commit()

mkdir("foo")
writefile("bar", "bar")
writefile("foo/a", "aaa")
writefile("foo/b", "bbb")
writefile("known/3", "unknown 3")

-- Default is recurse; recurse into both known and unknown
-- directories (as inventory does)
check(mtn("ls", "unknown"), 0, true, nil)
check(samelines("stdout",
{"bar",
 "emptyhomedir",
 "foo",
 "foo/a",
 "foo/b",
 "known/3",
 "min_hooks.lua",
 "tester.log"}))

-- respect --no-recursive
check(mtn("ls", "unknown", "--no-recursive"), 0, true, nil)
check(samelines("stdout",
{"bar",
 "emptyhomedir",
 "foo",
 "min_hooks.lua",
 "stdout",
 "tester.log"}))

-- Show contents of unknown directory, when the directory
-- is specified (issue 148 case 2)
check(mtn("ls", "unknown", "foo"), 0, true, nil)
check(samelines("stdout",
{"foo",
 "foo/a",
 "foo/b"}))

-- But not with --no-recursive
check(mtn("ls", "unknown", "foo", "--no-recursive"), 0, true, nil)
check(samelines("stdout",
{"foo"}))

-- Show contents of unknown path in unknown directory, 
-- when the path is specified (issue 148 case 1)
check(mtn("ls", "unknown", "foo/a"), 0, true, nil)
check(samelines("stdout",
{"foo/a"}))

-- From within an unknown directory, same as executing at root.
check(indir("foo", mtn("ls", "unknown")), 0, true, nil)
check(samelines("stdout",
{"bar",
 "emptyhomedir",
 "foo",
 "foo/a",
 "foo/b",
 "known/3",
 "min_hooks.lua",
 "stdout",
 "tester.log"}))

-- end of file
