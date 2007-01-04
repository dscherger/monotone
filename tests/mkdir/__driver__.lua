
skip_if(not existsonpath("test"))
mtn_setup()

--no args
check(mtn("mkdir"), 2, false, false)

--base functionality
check(mtn("mkdir", "testing"), 0, false, false)
check({"test", "-d", "testing"})

--error if already exists
check(mtn("mkdir", "testing"), 1, false, false)

--nested
check(mtn("mkdir", "testing2/nested"), 0, false, false)
check ({"test", "-d", "testing2/nested"})

--nested, parent exists
check(mtn("mkdir", "testing/nested"), 0, false, false)
check ({"test", "-d", "testing/nested"})

--multiple
check(mtn("mkdir", "test1", "test2"), 0, false, false)
check({"test", "-d", "test1"})
check({"test", "-d", "test2"})

--multiple, nested
check(mtn("mkdir", "testing1/test1", "testing2/test2"), 0, false, false)
check({"test", "-d", "testing1/test1"})
check({"test", "-d", "testing2/test2"})

--multiple, nested, same parent
check(mtn("mkdir", "testing3/test1", "testing3/test2"), 0, false, false)
check({"test", "-d", "testing3/test1"})
check({"test", "-d", "testing3/test2"})

--multiple w/some pre-existing
check(mtn("mkdir", "test1", "test3"), 1, false, false)
check({"test", "-d", "test3"}, 1, false, false)

--ignore feature
append(".mtn-ignore", "testing4\n")

check(mtn("mkdir", "testing4"), 1, false, false)
check({"test", "-d", "testing4"}, 1, false, false)

--multiple with some ignored (all should fail, workspace should be unchanged)
check(mtn("mkdir", "testing4", "testing5"), 1, false, false)
check({"test", "-d", "testing4"}, 1, false, false)
check({"test", "-d", "testing5"}, 1, false, false)

--not respecting ignore
check(mtn("--no-respect-ignore", "mkdir", "testing4"), 0, false, false)
check({"test", "-d", "testing4"})
