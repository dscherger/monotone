
mtn_setup()

-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

-- copy to filenames with invalid characters
copy("cvs-repository/test/foo,v",
     "cvs-repository/test/foo_\\,v")

copy("cvs-repository/test/foo,v",
     "cvs-repository/test/foo_ö,v")


-- import into monotone and check presence of files
xfail(mtn("--branch=testbranch", "cvs_import", "cvs-repository/test"), 0, false, false)

