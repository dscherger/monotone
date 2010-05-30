mtn_setup()

mkdir("parent1")
addfile("parent1/file1", "something")
commit()
orig = base_revision()

-- rename the parent 
check(mtn("rename", "parent1", "parent2"), 0, false, false)
commit()

-- alter the file
writefile("parent2/file1", "something else")
commit()

check(mtn("diff", "-r", orig), 0, true, false)
check(qgrep("^--- parent1/file1\t", "stdout"))
check(qgrep("^\\+\\+\\+ parent2/file1\t", "stdout"))

check(mtn("diff", "-r", orig, "parent2"), 0, true, false)
check(qgrep("^--- parent1/file1\t", "stdout"))
check(qgrep("^\\+\\+\\+ parent2/file1\t", "stdout"))

-- this fails because restricting to parent2/file1 excludes the rename
-- of parent1 to parent2 and when diff goes to get the file content
-- for file1 it looks in parent1/file1 rather than parent2/file1

-- the solution here is probably to make the restrictions code implicitly
-- include the parents, non-recursively, of all explicitly included nodes
-- then the parent rename would be included here and the diff would work.

check(mtn("diff", "-r", orig, "parent2/file1"), 0, false, false)
