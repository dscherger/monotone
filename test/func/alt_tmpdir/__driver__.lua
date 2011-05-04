-- Test the alternate tmpdir capability.
--
-- not a very thorough test; on Windows, files can be moved between
-- devices, and on Linux, it requires manual intervention to set up a
-- workspace that spans two devices. So we just verify that the
-- 'alt_tmpdir' keyword in _MTN/options is supported. The actual
-- operation is tested by use in real workspaces.

mtn_setup()

addfile("readme", "just a file")

mkdir("local")
addfile("local/local-file0", "local file 0\n")

mkdir("nfs_mounted")
addfile("nfs_mounted/nfs_mounted-file0", "nfs_mounted file 0\n")

commit()
rev0 = base_revision()

addfile("local/file1", "local file 1\n")
addfile("local/file2", "local file 2\n")

addfile("nfs_mounted/file1", "nfs_mounted file 1\n")
addfile("nfs_mounted/file2", "nfs_mounted file 2\n")

commit()
rev1 = base_revision()

revert_to(rev0)

get("options_line.text")
execute("sh", "-c", 'cat options_line.text >> _MTN/options') 

mkdir("nfs_mounted/tmp")

check(mtn("update"), 0, nil, true)

-- end of file
