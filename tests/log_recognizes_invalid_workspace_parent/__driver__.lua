
mtn_setup()

addfile("foo", "bar")
commit()

check(mtn("log"), 0, false, false)

rev = readfile("_MTN/revision")
-- change the parent revision to an invalid one
rev = string.gsub(rev,
    "old_revision %[(%w+)%]",
    "old_revision [0000000000000000000000000000000000000002]")
writefile("_MTN/revision", rev)

check(mtn("log"), 1, false, true)
check(qgrep(
    "workspace parent revision '0000000000000000000000000000000000000002' not found",
    "stderr"
))

