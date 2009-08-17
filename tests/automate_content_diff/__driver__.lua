
mtn_setup()

-- check output if there are no changes
check(mtn("automate", "content_diff"), 0, true, true)
check(fsize("stdout") == 0 and fsize("stderr") == 0)
check(mtn("automate", "content_diff", "--with-header"), 0, true, true)
check(fsize("stdout") == 0 and fsize("stderr") == 0)

-- check non-existing path
check(mtn("automate", "content_diff", "non_existing"), 1, true, true)


-- check existing path against current workspace
addfile("existing", "foo bar")
-- do not restrict here, since '' (the root) has not yet been committed
check(mtn("automate", "content_diff"), 0, true, true)
check(fsize("stdout") ~= 0)

-- add three more revisions and test for correct revid handling
commit()
R1=base_revision()
writefile("existing", "foo foo")
commit()
R2=base_revision()
writefile("existing", "foo foo bar")
commit()
R3=base_revision()

-- one and two revisions should work
check(mtn("automate", "content_diff", "-r", R1), 0, true, true)
check(fsize("stdout") ~= 0)
-- compare output order to --reverse below
check(qgrep("\\+\\+\\+ existing.*27f121005fcb075744d0c869183263c5b4814cb8", "stdout"))
check(qgrep("--- existing.*3773dea65156909838fa6c22825cafe090ff8030", "stdout"))

check(mtn("automate", "content_diff", "-r", R1, "-r", R2), 0, true, true)
check(fsize("stdout") ~= 0)
check(not qgrep("# patch", "stdout"))

check(mtn("automate", "content_diff", "-r", R1, "-r", R2, "--with-header"), 0, true, true)
check(fsize("stdout") ~= 0)
check(qgrep("# patch", "stdout"))

--  --reverse with one revision
check(mtn("automate", "content_diff", "-r", R1), 0, true, true)
check(qgrep("--- existing.*3773dea65156909838fa6c22825cafe090ff8030", "stdout"))
check(qgrep("\\+\\+\\+ existing.*27f121005fcb075744d0c869183263c5b4814cb8", "stdout"))

-- three and more should not
check(mtn("automate", "content_diff", "-r", R1, "-r", R2, "-r", R3), 1, true, true)

