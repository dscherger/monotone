mtn_setup()

-- see makerepo.sh
check(get("cvs-repository"))

check(mtn("--branch=testbranch", "cvs_import", "cvs-repository"), 0, false, false)
remove("_MTN")
check(mtn("co", "-r", "t:NASTY_TAG", "."), 0, false, false)

check(mtn("list", "known"), 0, true)
check(samelines("stdout", {"test",
                           "test/fileA",
                           "test/fileB",
                           "test/fileC",
                           "test/fileD",
                           "test/fileE",
                           "test/fileF"}))

check(mtn("list", "tags"), 0, true)

-- check contents at tag NASTY_TAG
check(samelines("test/fileA", {"version 0 of test fileA"}))
check(samelines("test/fileB", {"version 1 of test fileB"}))
check(samelines("test/fileC", {"version 2 of test fileC"}))
check(samelines("test/fileD", {"version 3 of test fileD"}))
check(samelines("test/fileE", {"version 4 of test fileE"}))
check(samelines("test/fileF", {"version 5 of test fileF"}))

