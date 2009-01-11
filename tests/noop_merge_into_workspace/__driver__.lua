-- Merging an ancestor or descendant into your workspace doesn't break things.
mtn_setup()

addfile("foo", "bar")
commit()
parent = base_revision()
addfile("baz", "qux")
commit()
child = base_revision()

check(mtn("merge_into_workspace", parent), 1, nil, false)

check(mtn("update", "--revision", parent), 0, false)
check(mtn("merge_into_workspace", child), 1, nil, false)
