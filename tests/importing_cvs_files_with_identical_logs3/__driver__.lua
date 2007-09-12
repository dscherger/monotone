
mtn_setup()

-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

-- import into monotone
check(mtn("--branch=testbranch", "cvs_import", "cvs-repository/test"), 0, false, false)

