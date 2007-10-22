
mtn_setup()

-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

-- import into monotone
xfail(mtn("--branch=testbranch", "cvs_import", "cvs-repository/test"), 0, false, false)

-- This fails, because some event times are unfortunately adjusted, so
-- that they exactly match another, conflicting event is the blob, which
-- needs to be split. Needs better get_best_split_point logic (i.e. split
-- a blob between two events with exactly the same timestamps).

