mtn_setup()

addfile("foo", "blabla")
-- ensure that commit no longer accepts -b
check(mtn("commit", "-bfoo"), 1, false, false)
-- do the commit for real now
commit()

-- check parameter checking
check(mtn("branch", "foo", "bar"), 2, false, false)

-- check if branch w/o parameters returns the current branch
check(mtn("branch"), 0, true, false)
canonicalize("stdout")
check(readfile("stdout") == "testbranch\n")

-- check if the workspace is already set to the existing branch
check(mtn("branch", "testbranch"), 1, false, true)
check(qgrep("workspace is already set to testbranch", "stderr"))

-- check for the correct wording (new vs existing)
check(mtn("branch", "new_branch"), 0, false, true)
check(qgrep("the new branch new_branch", "stderr"))

check(mtn("branch", "testbranch"), 0, false, true)
check(qgrep("the existing branch testbranch", "stderr"))

-- check if branch issues a warning if we're trying to change the
-- branch to a branch with an entirely independent base
check(mtn("automate", "put_file", "blablabla"), 0, true, false)
canonicalize("stdout")
fileid = string.sub(readfile("stdout"), 0, -2)
rev = [[
format_version "1"

new_manifest [0000000000000000000000000000000000000000]

old_revision []

add_dir ""

add_file "bla"
]]

rev = rev .. "content [" .. fileid .. "]\n"

check(mtn("automate", "put_revision", rev), 0, true, false)
canonicalize("stdout")
revid = string.sub(readfile("stdout"), 0, -2)
check(mtn("automate", "cert", revid, "branch", "other_branch"), 0, false, false)

check(mtn("branch", "other_branch"), 0, false, true)
check(qgrep("two unmergable heads", "stderr"))

