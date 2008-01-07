
mtn_setup()


-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

-- based on cvs_branches4, but this time with a conflicting
-- branch starting from:
--  - fileA @ 1.1.2.1 in branch A and
--  - fileB @ 1.1.4.1 in branch B.

-- import into monotone and check presence of files
check(mtn("--branch=test", "cvs_import", "--debug", "cvs-repository/test"), 0, false, false)

-- check names of imported branches
check(mtn("ls", "branches"), 0, true, false)
check(samelines("stdout", {
    "test",
    "test.A",
    "test.B",
    "test.CONFLICTING_BRANCH"
}))

-- checkout the conflicting test branch
check(mtn("checkout", "--branch=test.CONFLICTING_BRANCH", "mtcodir"), 0, false, false)

-- check if the log contains both branches from which this
-- conflicting branch was branched from
check(indir("mtcodir", mtn("log", "--no-graph", "--no-files")), 0, true, false)
check(qgrep("initial import", "stdout"))
check(qgrep("commit in branch A", "stdout"))
check(qgrep("commit in branch B", "stdout"))

-- check if the log for file1 only contains branch A from it
-- was branched into CONFLICTING_BRANCH
check(indir("mtcodir", mtn("log", "--no-graph", "--no-files", "file1")), 0, true, false)
check(qgrep("initial import", "stdout"))
check(qgrep("commit in branch A", "stdout"))
check(not qgrep("commit in branch B", "stdout"))

-- check if the log for file2 only contains branch B from it
-- was branched into CONFLICTING_BRANCH
check(indir("mtcodir", mtn("log", "--no-graph", "--no-files", "file2")), 0, true, false)
check(qgrep("initial import", "stdout"))
check(not qgrep("commit in branch A", "stdout"))
check(qgrep("commit in branch B", "stdout"))

