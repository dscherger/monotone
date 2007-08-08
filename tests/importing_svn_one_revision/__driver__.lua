
mtn_setup()


-- See makerepo.sh on how this repository was created.
check(get("svn-repository.dump"))

-- import into monotone
check(mtn("--branch=test", "svn_import", "svn-repository.dump"), 0, false, false)
