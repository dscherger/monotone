
mtn_setup()

check(get("cvs-repository"))

check(mtn("--branch=foo.bar", "cvs_import", "cvs-repository"), 0, false, false)
check(mtn("--branch=foo.bar", "checkout"), 0, false, true)

check(indir("foo.bar", mtn("list", "known")), 0, true)
check(samelines("stdout", {"t", "t/libasm", "t/libasm/ChangeLog",
                           "t/libelf-po", "t/libelf-po/POTFILES.in"}))

check(indir("foo.bar", mtn("list", "tags")), 0, true)
check(grep("initial", "stdout"), 0, false, false)
check(grep("portable-branch-base", "stdout"), 0, false, false)
check(grep("portable-branch-fork", "stdout"), 0, false, false)
