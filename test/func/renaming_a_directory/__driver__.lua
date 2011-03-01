-- Test various rename directory cases

mtn_setup()

includecommon("basic_io.lua")
includecommon("test_utils_inventory.lua")

mkdir("foo")

writefile("foo/foo", "foo file")
writefile("bleh", "bleh file")

-- produce root
check(mtn("add", "-R", "foo"), 0, false, false)
commit()
root_r_sha = base_revision()
root_f_sha = sha1("foo/foo")

-- produce move edge
check(mtn("rename", "--bookkeep-only", "foo", "bar"), 0, false, false)
rename("foo", "bar")
commit()

-- revert to root
probe_node("foo/foo", root_r_sha, root_f_sha)
remove("bar")

-- make an add *into the directory*
addfile("foo/bar", "bar file")
commit()

-- merge the add and the rename
check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)
check(mtn("automate", "get_manifest_of"), 0, true, false)
rename("stdout", "manifest")
check(qgrep("bar/bar", "manifest"))
check(qgrep("bar/foo", "manifest"))
check(not qgrep("foo/bar", "manifest"))
check(not qgrep("foo/foo", "manifest"))
check(exists("bar/bar"))
check(exists("bar/foo"))

-- Now do directory rename in other order; mtn 0.48 got this wrong
rename("bar", "foo")
check(mtn("rename", "--bookkeep-only", "bar", "foo"), 0, false, false)
check(mtn("automate", "inventory", "--no-unknown"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))

check_inventory (parsed, 7,
{path = "bar",
 old_type = "directory",
 new_path = "foo",
 fs_type = "none",
 status = {"rename_source"}})

check_inventory (parsed, 22,
{path = "foo",
 new_type = "directory",
 old_path = "bar",
 fs_type = "directory",
 birth = root_r_sha,
 status = {"rename_target", "known"}})

