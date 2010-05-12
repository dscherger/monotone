
mtn_setup()

addfile("foo", "foo\n")
addfile("bar", "bar\n")

commit()
REV1=base_revision()

writefile("foo", "foo changed\n")

commit()
REV2=base_revision()

addfile("quux", "quux\n")

commit()
REV3=base_revision()

writefile("foo", "foo again\n")
writefile("bar", "bar again\n")

check(mtn("rename", "foo", "foo2"), 0, false, false)

commit()
REV4=base_revision()

-- without restrictions
check(mtn("log", "--diffs", "--no-graph"), 0, true, false)
check(grep('^(---|\\+\\+\\+) ', "stdout"), 0, true, false)
rename("stdout", "full")
check(get("expect_full"))
canonicalize("full")
check(samefile("expect_full", "full"))

-- restrict to foo2 and quux
check(mtn("log", "--no-graph", "quux", "foo2", "--diffs", "--from", REV4, "--to", REV1), 0, true, false)
check(grep("^(---|\\+\\+\\+) ", "stdout"), 0, true, false)
rename("stdout", "restrict")
check(get("expect_restrict"))
canonicalize("restrict")
check(samefile("expect_restrict", "restrict"))

-- this fails because restricting to quux and foo2 excludes the
-- addition of the root dir in the initial revision.

-- the solution here is probably to make the restrictions code implicitly
-- include the parents, non-recursively, of all explicitly included nodes
-- then the parent rename would be included here and the diff would work.

xfail(mtn("log", "-r", REV1, "quux", "foo2", "--diffs"), 0, false, false)
