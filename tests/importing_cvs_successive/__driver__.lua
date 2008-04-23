
mtn_setup()

-- A first simple test on successive imports from CVS. After
-- importing a first snapshot of a CVS repository, we try to
-- continue the import from that point on.
--
-- Compared with the first import, this tests addsd a tag to
-- an existing revision, a branch and two new revisions.


-- See makerepo.sh on how this repository and the first
-- snapshot was created.
check(get("cvs-repository-snap"))
check(get("cvs-repository"))

-- import the snapshot
check(mtn("--branch=test", "cvs_import", "cvs-repository-snap/test"), 0, false, false)

-- then run a consecutive import on the full repository
xfail(mtn("--branch=test", "cvs_import", "cvs-repository/test"), 0, false, false)

