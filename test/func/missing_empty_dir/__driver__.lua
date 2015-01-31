
mtn_setup()

mkdir("foo")
mkdir("bar")

check(mtn("add", "foo"), 0, false, true)
check(mtn("add", "bar"), 0, false, true)
commit()

remove("foo")

check(mtn("status"), 0, true, false)
check(qgrep("missing directory: foo", "stdout"))

writefile("foo", "foo")

check(mtn("status"), 0, true, false)
check(qgrep("not a directory:   foo", "stdout"))
