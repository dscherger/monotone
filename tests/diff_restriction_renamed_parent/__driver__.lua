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

check(mtn("diff", "-r", orig, "parent2/file1"), 0, false, false)
