
mtn_setup()

-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

-- try an import, it currently still fails, due to duplicate RCS files in
-- attic and live directory.
xfail(mtn("--branch=testbranch", "cvs_import", "cvs-repository/test"), 0, false, false)

