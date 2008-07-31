
mtn_setup()

-- This is the same repository as in 'importing_cvs_files', except that
-- the delta for file foo @ 1.3 as been manually removed.

check(get("cvs-repository"))

-- try an import...
check(mtn("--ticker", "none", "--branch=testbranch", "cvs_import", "cvs-repository/test"), 1, false, true)
check(samelines("stderr", {
    "mtn: parsing rcs files",
    "mtn: error: delta for revision 1.3 is missing"
}))

