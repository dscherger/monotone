
mtn_setup()

-- This test is based on importing_cvs_with_tags_after_delete, but
-- has an additional timestamp issue. Due to timestamp adjustment, the
-- file bar is suddenly alive in the tagged revision.
--
-- To avoid that error, we would have to make sure to not include a
-- file in a tag which does not have the tag in it's RCS file. I.e.
-- bar,v does not carry the FOO_ONLY tag.

check(get("cvs-repository"))

-- import and check
check(mtn("--branch=test", "cvs_import", "cvs-repository/test"), 0, false, false)

check(mtn("checkout", "--revision=FOO_AND_BAR", "mtnco"), 0, false, false)
check(indir("mtnco", mtn("list", "known")), 0, true, false)
check(samelines("stdout", {"bar", "foo"}))
remove("mtnco")

check(mtn("checkout", "--revision=FOO_ONLY", "mtnco"), 0, false, false)
check(indir("mtnco", mtn("list", "known")), 0, true, false)
xfail(samelines("stdout", {"foo"}))

