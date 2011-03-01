
mtn_setup()

-- If a binary file is removed, then 'mtn diff' should display:
--   # binaryfilename is binary
-- not:
--   # /dev/null is binary

check(get("binary"))

check(mtn("add", "binary"), 0, false, false)
commit()

check(mtn("drop", "binary"), 0, false, false)
check(not exists("binary"))
check(mtn("diff"), 0, true, false)

check(qgrep("# binary is binary", "stdout"))
