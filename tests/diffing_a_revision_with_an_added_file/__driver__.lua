
mtn_setup()

addfile("foo1", "foo file 1")
commit()
parent = base_revision()

addfile("foo2", "foo file 2")
id2=sha1("foo2")
commit()

check(mtn("diff", "--revision", parent, "--revision", base_revision()), 0, true, false)
check(qgrep("^--- /dev/null\t$", "stdout"))
check(qgrep("^\\+\\+\\+ foo2\t" .. id2 .. "$", "stdout"))
