
mtn_setup()

writefile("foo", "foo file")
writefile("bleh", "bleh file")

-- produce root
addfile("foo")
commit()
root_r_sha = base_revision()
root_f_sha = sha1("foo")

-- produce move edge
check(mtn("rename", "foo", "bar"), 0, false, false)
copy("foo", "bar")
commit()

-- revert to root
probe_node("foo", root_r_sha, root_f_sha)
remove("bar")

-- make a simple add edge
addfile("bleh")
commit()

-- merge the add and the rename
check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)
check(mtn("automate", "get_manifest_of"), 0, true, false)
rename("stdout", "manifest")
check(qgrep("bar", "manifest"))
check(qgrep("bleh", "manifest"))

-- rename a rename target
check(mtn("rename", "bleh", "blah"), 0, false, false)
check(qgrep("bleh", "_MTN/workrev"))
check(qgrep("blah", "_MTN/workrev"))
check(mtn("rename", "blah", "blyh"), 0, false, false)
check(qgrep("bleh", "_MTN/workrev"))
check(not qgrep("blah", "_MTN/workrev"))
check(qgrep("blyh", "_MTN/workrev"))

-- undo a rename
check(mtn("rename", "blyh", "bleh"), 0, false, false)
check(not qgrep("blyh", "_MTN/workrev"))
check(not qgrep("bleh", "_MTN/workrev"))

-- move file before renaming it
check(mtn("status"), 0, false, false)
rename("bar", "barfoo")
check(mtn("rename", "bar", "barfoo"), 0, false, true)
check(qgrep('renaming bar to barfoo in workspace manifest', "stderr"))
check(mtn("status"), 0, false, false)

-- move file to wrong place before renaming it
rename("barfoo", "bar")
check(mtn("revert", "."), 0, false, false)
check(mtn("status"), 0, false, false)
rename("bar", "barfoofoo")
check(mtn("rename", "bar", "barfoo"), 0, false, true)
check(qgrep('renaming bar to barfoo in workspace manifest', "stderr"))
check(mtn("status"), 1, false, false)
