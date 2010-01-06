mtn_setup()

addfile("r1", "111")
commit("testbranch", "r1")
r1 = base_revision()

addfile("r2", "222")
commit("testbranch", "r2")
r2 = base_revision()

addfile("r3", "333")
commit("testbranch", "r3")
r3 = base_revision()

addfile("r4", "444")
commit("testbranch", "r4")
r4 = base_revision()

addfile("r5", "555")
commit("testbranch", "r5")
r5 = base_revision()

check(mtn("log", "--no-files", "--from", r1, "--next", 5), 0, false, false)

check(mtn("update", "--revision", r1), 0, false, false)
check(mtn("bisect", "good"), 0, false, false)

writefile("r2", "r2-blocked")

check(mtn("bisect", "bad", "--revision", r5), 1, false, false)
check(base_revision() == r1)

check(mtn("bisect", "status"), 0, false, true)
check(qgrep("warning:", "stderr"))

check(mtn("bisect", "update"), 1, false, false)
check(base_revision() == r1)

check(mtn("bisect", "update", "--move-conflicting-paths"), 0, false, false)
check(base_revision() == r3)


check(mtn("update", "--revision", r1), 0, false, false)
check(mtn("bisect", "reset"), 0, false, false)
remove("_MTN/resolutions")

check(mtn("bisect", "good"), 0, false, false)

writefile("r2", "r2-blocked")

check(mtn("bisect", "bad", "--revision", r5), 1, false, false)
check(base_revision() == r1)

check(mtn("bisect", "bad", "--revision", r5, "--move-conflicting-paths"), 0, false, false)
check(base_revision() == r3)

