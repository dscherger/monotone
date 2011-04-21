
mtn_setup()

addfile("testfile", "blah blah")
commit()
R1=base_revision()

writefile("testfile", "foo foo")
commit()
R2=base_revision()

check(mtn("tag", R1, "ambig_tag"), 0, false, false)
check(mtn("tag", R2, "ambig_tag"), 0, false, false)

-- abbreviated revision ids

a1=string.sub(R1, 0, 10) .. "..."
a2=string.sub(R2, 0, 10) .. "..."

check(mtn("ls", "tags"), 0, true, false)
check(qgrep(a1, "stdout"))
check(qgrep(a2, "stdout"))
