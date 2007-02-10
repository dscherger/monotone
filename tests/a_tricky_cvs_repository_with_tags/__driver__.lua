
mtn_setup()

check(get("e"))

check(mtn("--branch=foo.bar", "cvs_import", "e"), 0, false, false)
check(mtn("--branch=foo.bar", "co"))

check(indir("foo.bar", mtn("list", "known")), 0, true)
check(samelines("stdout", {"t", "t/libasm", "t/libasm/ChangeLog",
                           "t/libelf-po", "t/libelf-po/POTFILES.in"}))

check(indir("foo.bar", mtn("list", "tags")), 0, true)
check(grep("initial", "stdout"), 0, false, false)
check(grep("portable-branch-base", "stdout"), 0, false, false)
check(grep("portable-branch-fork", "stdout"), 0, false, false)
