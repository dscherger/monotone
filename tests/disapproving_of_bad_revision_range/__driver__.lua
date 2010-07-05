
mtn_setup()

addfile("testfile","dummy content")
check(mtn("add", "testfile"), 0, false, false)
commit()
root_r_sha = base_revision()

check(mtn("disapprove", root_r_sha, "deadcafe"), 1, false, false)
