
mtn_setup()

getfile("rootfile", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()
root_r_sha = base_revision()
root_f_sha = sha1("testfile")

getfile("goodfile", "testfile")
commit()
left_good_r_sha = base_revision()
left_good_f_sha = sha1("testfile")
check(left_good_r_sha ~= root_r_sha)
check(left_good_f_sha ~= root_f_sha)

getfile("badfile", "testfile")
commit()
left_bad_r_sha = base_revision()
left_bad_f_sha = sha1("testfile")
check(left_bad_r_sha ~= left_good_r_sha)
check(left_bad_f_sha ~= left_good_f_sha)

probe_node("testfile", root_r_sha, root_f_sha)

getfile("updatefile", "testfile")
check(mtn("disapprove", left_bad_r_sha), 0, false, false)
check(mtn("update"), 0, false, false)

-- files should now be merged

getfile("finalfile", "probe")
check(samefile("testfile", "probe"))
