
mtn_setup()

check(get("rootfile", "testfile"))
check(mtn("add", "testfile"), 0, false, false)
commit()
root_r_sha = base_revision()
root_f_sha = sha1("testfile")

check(mtn("disapprove", root_r_sha, "deadcafe"), 1, false, false)
