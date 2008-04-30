mtn_setup()

addfile("testfile", "this is just a file\n")
commit("testbranch")
first = base_revision()

writefile("testfile", "Now, this is a different file\n")
commit("testbranch")
second = base_revision()

writefile("testfile", "And we change it a third time\n")
commit("testbranch")
third = base_revision()

-- We now have this:
--
--          1
--         /
--        2
--       /
--      3

check(mtn("update", "-r"..first), 0, false, false)
addfile("testfile2", "A new file in a new branch")
commit("testbranch2")
b2_first = base_revision()

check(mtn("update", "-r"..second), 0, false, false)
addfile("testfile3", "Another file in the new branch")
commit("testbranch2")
b2_second = base_revision()

-- We now have this:
--
--          1
--         / \.				(periods to prevent multi-line
--        2   b2_1			comment warnings)
--       / \.
--      3   b2_2

-- At this point, the LCA should become revision 1.  We use h: as the
-- selector because it can be fed with a glob expression.
check(mtn("automate","select","L:h:{testbranch,testbranch2}"), 0, true, false)
check(qgrep(first, "stdout"))

-- Now, let's merge the second branch and select the LCA again.  The
-- result should now be revision 2.
check(mtn("merge","-btestbranch2"), 0, false, false)
check(mtn("automate","select","L:h:{testbranch,testbranch2}"), 0, true, false)
check(qgrep(second, "stdout"))
