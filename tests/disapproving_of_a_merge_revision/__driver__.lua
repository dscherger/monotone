
mtn_setup()

check(get("rootfile", "testfile"))
check(mtn("add", "testfile"), 0, false, false)
commit()
root_r_sha = base_revision()

check(get("goodfile", "testfile"))
commit()
left_good_r_sha = base_revision()
check(left_good_r_sha ~= root_r_sha)

check(get("badfile", "testfile"))
commit()
left_bad_r_sha = base_revision()
check(left_bad_r_sha ~= left_good_r_sha)

check(mtn("update","-r",left_good_r_sha), 0, false, false)
check(get("rootfile", "testfile2"))
check(mtn("add","testfile2"), 0, false, false)
commit()
check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false,false)


check(get("badfile2", "testfile"))
commit()
left_bad2_r_sha = base_revision()
check(left_bad2_r_sha ~= left_good_r_sha)

check(mtn("disapprove", root_r_sha, left_bad2_r_sha), 1, false, false)
-- disapprove should fail because there is a merge

