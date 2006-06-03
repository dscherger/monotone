
mtn_setup()

mkdir("places")
addfile("maude", "the file maude")
addfile("harold", "the file harold")
addfile("places/cemetery", "the place file cemetery")
commit()

os.remove("maude")

check(mtn("drop", "maude"), 0, false, true)
check(qgrep('dropping maude from workspace manifest', "stderr"))

check(mtn("status"), 0, true)
check(qgrep("maude", "stdout"))
check(not qgrep("harold", "stdout"))
check(not qgrep("places/cemetery", "stdout"))

check(mtn("drop", "foobar"), 0, false, true)
check(qgrep("skipping foobar", "stderr"))

os.remove("harold")
os.remove("places/cemetery")

check(mtn("drop", "--missing"), 0, false, true)
check(qgrep('dropping harold from workspace manifest', "stderr"))
check(qgrep('dropping places/cemetery from workspace manifest', "stderr"))

check(mtn("status"), 0, true)
check(qgrep("maude", "stdout"))
check(qgrep("harold", "stdout"))
check(qgrep("places/cemetery", "stdout"))
