
-- remove "x/a.txt" - so it becomes missing
-- then "mtn ls changed y"
-- we should get no output - as there are no changes in "y"

mtn_setup()

mkdir("x")
mkdir("y")

writefile("x/a.txt", "a")
writefile("x/b.txt", "b")
writefile("y/a.txt", "a")
writefile("y/b.txt", "b")

check(mtn("add", "x/a.txt"), 0, false, false)
check(mtn("add", "x/b.txt"), 0, false, false)
check(mtn("add", "y/a.txt"), 0, false, false)
check(mtn("add", "y/b.txt"), 0, false, false)

commit()

remove("x/a.txt")

check(mtn("ls", "changed", "y"), 0, nil, false)
