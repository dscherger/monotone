-- Test 'automate checkout'
include("/common/automate_stdio.lua")

mtn_setup()

mkdir("work_1")
rename("_MTN", "work_1/_MTN")
chdir("work_1")

----------
-- create a revision to checkout

addfile("file_1", "file_1")
addfile("file_2", "file_2")
addfile("file_3", "file_3")
commit()
rev1 = base_revision()

-- normal automate
check(mtn("automate", "checkout", "--branch", "testbranch", "../work_2"), 0, false, false)

-- automate stdio
progress = run_stdio(make_stdio_cmd({"checkout", "../work_3"}, {{"branch", "testbranch"}}), 0, 0, 'p')

-- Command error cases

-- Too many arguments
check(mtn("automate", "checkout", "foo", "bar"), 1, false, true)
check(qgrep("wrong argument count", "stderr"))

-- Too many revisions
writefile("file_1", "file_1, 2")
commit()
rev2 = base_revision()
check(mtn("automate", "checkout", "-r", rev1, "-r", rev2), 1, false, true)
check(qgrep("wrong revision count", "stderr"))

-- other errors are handled by the same code as 'mtn checkout', so don't
-- need to be tested here.

-- end of file
