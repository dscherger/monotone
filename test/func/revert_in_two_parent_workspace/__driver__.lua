-- create a workspace with 2 parents then revert to just one of them
-- and check the workspace is consistent with it

mtn_setup()

addfile("testfile", "ancestor\nancestor")
addfile("otherfile", "blah blah")
commit()
anc = base_revision()

writefile("testfile", "left\nancestor")
writefile("otherfile", "modified too")
commit()
left = base_revision()

revert_to(anc)
writefile("testfile", "ancestor\nright")
commit()
right = base_revision()

check(mtn("merge_into_workspace", left), 0, false, false)
check(samelines("testfile", { "left", "right" } ))
check(samelines("otherfile", { "modified too" } ))

-- we now have workspace with 2 parents, so lets try a revert
-- without specifying a revision - mtn should throw an error

check(mtn("revert", "."), 1, nil, true)
need_to_specify_diag = "mtn: misuse: this workspace has multiple parents. specify which parent to use with -r\n"
check(grep("-v", "detected at", "stderr"), 0, need_to_specify_diag)

-- now lets try a revert by specifying an invalid parent revision.
-- the revision id MUST be valid - it just needs to NOT be a parent of the
-- current workspace. if the revision id is not valid, mtn will throw an error
-- about "no match for selection" - which we don't care about in this test

check(mtn("revert", ".", "-r", anc), 1, nil, true)
incorrect_parent_diag = "mtn: misuse: the specified revision is not a parent of the current workspace\n"
check(grep("-v", "detected at", "stderr"), 0, incorrect_parent_diag)

-- now lets revert back to a proper revision - and check the workspace
-- contents
check(mtn("revert", ".", "-r", left), 0, nil, true)
check(samelines("testfile", { "left", "ancestor" } ))
check(samelines("otherfile", { "modified too" } ))

-- TODO: file contents are fine - now lets check if the book keeping is
-- correct by running "mtn status" and "mtn ls changed" - there should
-- be no changes



-- TODO: perform revert to right as well
-- check(samelines("testfile", { "ancestor", "right" } )
-- check(samelines("otherfile", { "blah blah" } )

