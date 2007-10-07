--
-- This test includes conflicting tags, as follows:
--
--    ANOTHER TAG:     fileA @ 1.1   - Version 0 of fileA
--                     fileB @ 1.2   - Version 1 of fileB
--
--    CONFLICTING_TAG: fileA @ 1.2   - Version 1 of fileA
--                     fileB @ 1.1   - Version 0 of fileB
--
-- To be able to correctly represent those two revisions, we need to
-- insert artificial revisions.
--

mtn_setup()

check(get("cvs-repository"))

check(mtn("--branch=foo.bar", "cvs_import", "cvs-repository"), 0, false, false)
check(mtn("--branch=foo.bar", "co"))

check(indir("foo.bar", mtn("list", "known")), 0, true)
check(samelines("stdout", {"testsrc", "testsrc/fileA", "testsrc/fileB"}))

check(indir("foo.bar", mtn("list", "tags")), 0, true)
check(grep("initial", "stdout"), 0, false, false)
check(grep("CONFLICTING_TAG", "stdout"), 0, false, false)
check(grep("ANOTHER_TAG", "stdout"), 0, false, false)

-- check contents at tag ANOTHER_TAG
check(indir("foo.bar", mtn("update", "-r", "ANOTHER_TAG")), 0, false, false)
check(indir("foo.bar", mtn("list", "known")), 0, true)
check(samelines("stdout", {"testsrc", "testsrc/fileA", "testsrc/fileB"}))
check(samelines("foo.bar/testsrc/fileA", {"Version 0 of fileA."}))
xfail(samelines("foo.bar/testsrc/fileB", {"Version 1 of fileB."}))

-- Currently, ANOTHER_TAG gets split correctly, but then we simply override
-- ANOTHER_TAG as soon as the second blob with the same tag is consumed.


-- check contents at tag CONFLICTING_TAG
check(indir("foo.bar", mtn("update", "-r", "CONFLICTING_TAG")), 0, false, false)
check(indir("foo.bar", mtn("list", "known")), 0, true)
check(samelines("stdout", {"testsrc", "testsrc/fileA", "testsrc/fileB"}))
check(samelines("foo.bar/testsrc/fileA", {"Version 1 of fileA."}))
check(samelines("foo.bar/testsrc/fileB", {"Version 0 of fileB."}))

