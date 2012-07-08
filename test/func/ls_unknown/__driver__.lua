-- Test various cases of 'ls unknown'

mtn_setup()

mkdir("foo")
writefile("bar", "bar")
writefile("foo/a", "aaa")
writefile("foo/b", "bbb")

-- Doesn't recurse into unknown directory
check(mtn("ls", "unknown"), 0, true, nil)
check(samelines("stdout",
{"bar",
 "emptyhomedir",
 "foo",
 "min_hooks.lua"}))

-- Doesn't show contents of unknown directory, even when the directory is specified
check(mtn("ls", "unknown", "foo"), 0, true, nil)
check(samelines("stdout",
{"foo"}))

-- From within an unknown directory, same as executing at root.
check(indir("foo", mtn("ls", "unknown")), 0, true, nil)
check(samelines("stdout",
{"bar",
 "emptyhomedir",
 "foo",
 "min_hooks.lua"}))


