
mtn_setup()


-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

-- based on cvs_branches4, but this time with a tag C on
-- fileA @ 1.1.2.1 in branch A and fileB @ 1.1.4.1 in
-- branch B.

-- import into monotone and check presence of files
xfail(mtn("--branch=test", "cvs_import", "cvs-repository/test"), 0, false, false)

