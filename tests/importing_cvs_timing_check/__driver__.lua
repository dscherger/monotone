
mtn_setup()

-- This test checks if the blobs are imported in the correct order (after
-- having toposorted them). Importing the repository gives the following
-- blob graph:
--
--                     A
--                   /    \
--                  B      C
--                  |      |
--                  E      D
--                    \   /
--                      F
--
-- The correct import order should be: A B C D E F, but a topological
-- sort (i.e. reversed post-ordering of a depth first search) returns
-- either A B E C D F or A C D B E F.


-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

-- import into monotone
check(mtn("--branch=testbranch", "cvs_import", "cvs-repository/test"), 0, false, false)

-- check for correct ordering of the commits
check(mtn("checkout", "--branch=testbranch", "mtcodir"), 0, false, false)
check(indir("mtcodir", mtn("log", "--no-graph", "--no-files")), 0, true, false)
check(grep("blob", "stdout"), 0, true, false)
check(samelines("stdout", {"blob F", "blob E", "blob D",
                           "blob C", "blob B", "blob A"}))

