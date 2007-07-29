
mtn_setup()

check(get("cvs-repository"))

xfail(mtn("--branch=foo.bar", "cvs_import", "cvs-repository"), 0, false, false)
check(mtn("--branch=foo.bar", "co"))

check(indir("foo.bar", mtn("list", "known")), 0, true)
check(samelines("stdout", {"testsrc", "testsrc/fileA", "testsrc/fileB"}))

check(indir("foo.bar", mtn("list", "tags")), 0, true)
check(grep("initial", "stdout"), 0, false, false)
check(grep("CONFLICTING_TAG", "stdout"), 0, false, false)
check(grep("ANOTHER_TAG", "stdout"), 0, false, false)

check(indir("foo.bar", mtn("update", "-r", "CONFLICTING_TAG")), 0, false, false)
check(indir("foo.bar", mtn("list", "known")), 0, true)
check(samelines("stdout", {"testsrc", "testsrc/fileA", "testsrc/fileB"}))
check(samelines("foo.bar/testsrc/fileA", {"Version 1 of fileA."}))
check(samelines("foo.bar/testsrc/fileB", {"Version 0 of fileB."}))
