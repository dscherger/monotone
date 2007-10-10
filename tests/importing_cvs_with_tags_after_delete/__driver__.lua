
mtn_setup()

-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

-- A test case inspired by Marko Macek from the dev@cvs2svn mailing list:
-- http://cvs2svn.tigris.org/servlets/ReadMsg?list=dev&msgNo=1877
--
-- It creates two files, foo and bar, tags then deletes bar and tags
-- again. The monotone repository is then checked for correct files
-- in the tagged revisions.

-- import and check
check(mtn("--branch=test", "cvs_import", "cvs-repository/test"), 0, false, false)

check(mtn("checkout", "--revision=FOO_AND_BAR", "mtnco"), 0, false, false)
check(indir("mtnco", mtn("list", "known")), 0, true, false)
check(samelines("stdout", {"bar", "foo"}))
remove("mtnco")

check(mtn("checkout", "--revision=FOO_ONLY", "mtnco"), 0, false, false)
check(indir("mtnco", mtn("list", "known")), 0, true, false)
xfail(samelines("stdout", {"foo"}))

-- This one is hard to solve: file bar should not be included in the tagged
-- revision, but that can only be figured out, if we look at what tags
-- the RCS file has. This might be erroneous in other cases, where a file
-- *should* be included in a revision, but somehow misses the tag.

