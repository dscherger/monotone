
mtn_setup()

addfile("testfile", "blah blah")
commit()
R1=base_revision()

writefile("testfile", "foo foo")
commit()
R2=base_revision()

writefile("testfile", "bar bar")
commit()
R3=base_revision()

writefile("testfile", "baz baz")
commit()
R4=base_revision()

check(mtn("tag", R1, "ambig_tag"), 0, false, false)
check(mtn("tag", R2, "ambig_tag"), 0, false, false)
check(mtn("tag", R3, "test_tag"), 0, false, false)
check(mtn("tag", R4, "other_tag"), 0, false, false)

-- abbreviated revision ids

a1=string.sub(R1, 0, 10) .. "..."
a2=string.sub(R2, 0, 10) .. "..."
a3=string.sub(R3, 0, 10) .. "..."
a4=string.sub(R4, 0, 10) .. "..."

check(mtn("ls", "tags"), 0, true, false)
check(qgrep(a1, "stdout"))
check(qgrep("ambig_tag", "stdout"))
check(qgrep(a2, "stdout"))
check(qgrep("ambig_tag", "stdout"))
check(qgrep(a3, "stdout"))
check(qgrep("test_tag", "stdout"))
check(qgrep(a4, "stdout"))
check(qgrep("other_tag", "stdout"))

check(mtn("db", "kill_tag_locally", "test_tag"), 0, false, false)

check(mtn("ls", "tags"), 0, true, false)
check(qgrep(a1, "stdout"))
check(qgrep("ambig_tag", "stdout"))
check(qgrep(a2, "stdout"))
check(qgrep("ambig_tag", "stdout"))
check(not qgrep(a3, "stdout"))
check(not qgrep("test_tag", "stdout"))
check(qgrep(a4, "stdout"))
check(qgrep("other_tag", "stdout"))

check(mtn("db", "kill_tag_locally", "ambig_tag"), 0, false, false)

check(mtn("ls", "tags"), 0, true, false)
check(not qgrep(a1, "stdout"))
check(not qgrep("ambig_tag", "stdout"))
check(not qgrep(a2, "stdout"))
check(not qgrep("ambig_tag", "stdout"))
check(not qgrep(a3, "stdout"))
check(not qgrep("test_tag", "stdout"))
check(qgrep(a4, "stdout"))
check(qgrep("other_tag", "stdout"))
