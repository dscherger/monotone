
mtn_setup()

mkdir("foo")
mkdir("foo/bar")
writefile("testfile0", "version 0 of first test file\n")
writefile("foo/testfile1", "version 0 of second test file\n")
writefile("foo/bar/testfile2", "version 0 of third test file\n")
getfile("manifest")
canonicalize("manifest")

check(cmd(mtn("add", "testfile0", "foo")), 0, false, false)
commit()
check(cmd(mtn("automate", "get_manifest_of")), 0, true)
canonicalize("stdout")
check(samefile("stdout", "manifest"))
