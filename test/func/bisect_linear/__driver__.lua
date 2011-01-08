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

-- test 1: odd number of revs ending on a bad revision

check(mtn("bisect", "good", "--revision", r1), 0, false, false)
check(mtn("bisect", "bad", "--revision", r5), 0, false, false)
check(base_revision() == r3)
check(mtn("bisect", "bad"), 0, false, false)
check(base_revision() == r2)
check(mtn("bisect", "bad"), 0, false, false)
check(base_revision() == r2)

-- redundant
check(mtn("bisect", "bad"), 0, false, false)
check(base_revision() == r2)

-- conflicting
check(mtn("bisect", "good"), 1, false, false)
check(base_revision() == r2)

check(mtn("bisect", "status"), 0, false, false)

-- test 2: ending on a good revision updates to the first bad revision

check(mtn("bisect", "reset"), 0, false, false)
check(mtn("bisect", "good", "--revision", r1), 0, false, false)
check(mtn("bisect", "bad", "--revision", r5), 0, false, false)
check(base_revision() == r3)
check(mtn("bisect", "bad"), 0, false, false)
check(base_revision() == r2)
check(mtn("bisect", "good"), 0, false, false)
check(base_revision() == r3)

-- redundant
check(mtn("bisect", "bad"), 0, false, false)
check(base_revision() == r3)

-- conflicting
check(mtn("bisect", "good"), 1, false, false)
check(base_revision() == r3)

check(mtn("bisect", "status"), 0, false, false)

-- test 3: even number of revs ending on a bad revision

check(mtn("bisect", "reset"), 0, false, false)
check(mtn("bisect", "good", "--revision", r1), 0, false, false)
check(mtn("bisect", "bad", "--revision", r4), 0, false, false)
check(base_revision() == r3)
check(mtn("bisect", "bad"), 0, false, false)
check(base_revision() == r2)
check(mtn("bisect", "bad"), 0, false, false)
check(base_revision() == r2)

-- redundant
check(mtn("bisect", "bad"), 0, false, false)
check(base_revision() == r2)

-- conflicting
check(mtn("bisect", "good"), 1, false, false)
check(base_revision() == r2)

check(mtn("bisect", "status"), 0, false, false)

-- test 4: ending on a good revision updates to the first bad revision

check(mtn("bisect", "reset"), 0, false, false)
check(mtn("bisect", "good", "--revision", r1), 0, false, false)
check(mtn("bisect", "bad", "--revision", r4), 0, false, false)
check(base_revision() == r3)
check(mtn("bisect", "bad"), 0, false, false)
check(base_revision() == r2)
check(mtn("bisect", "good"), 0, false, false)
check(base_revision() == r3)

-- redundant
check(mtn("bisect", "bad"), 0, false, false)
check(base_revision() == r3)

-- conflicting
check(mtn("bisect", "good"), 1, false, false)
check(base_revision() == r3)

check(mtn("bisect", "status"), 0, false, false)

