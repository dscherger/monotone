-- Test for issue 201
--
-- original problem:
--
-- mtn pull with branch pattern does not pull branch certs from
-- propagate that don't match pattern. So if there are revs that are
-- pulled because of parent relationships, but only have branch certs
-- that don't match the pattern, mtn db check reports missing branch
-- certs as serious problem.

includecommon("netsync.lua")
mtn_setup("testbranch")
netsync.setup()

addfile("testfile", "blah stuff")
commit()
ver0 = base_revision()

addfile("testfile2", "some more data")
commit("otherbranch")
ver1 = base_revision()

check(mtn("propagate", "testbranch", "otherbranch"), 0, nil, true)

netsync.pull("otherbranch")

-- Now ver0 has no branch cert in mtn2:
check(mtn("ls", "certs", ver0), 0, true, nil)
check(qgrep("Name  : branch", "stdout"))

check(mtn2("ls", "certs", ver0), 0, true, nil)
check(not qgrep("Name  : branch", "stdout"))

-- and db check says this is a serious problem, but we'd like it not to.
xfail(mtn2("db", "check"), 0, nil, true)
check(qgrep("missing branch cert", "stderr"))
check(qgrep("error: serious problems detected", "stderr"))
