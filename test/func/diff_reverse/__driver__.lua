-- Test --reverse option for diff

mtn_setup()

addfile("file1", "1: data 1\n")
commit()
rev = base_revision()

writefile("file1", "1: data 2\n")

-- illegal --reverse option
check(mtn("diff", "--reverse"), 1, false, true)
check(qgrep("'--reverse' only allowed with exactly one revision", "stderr"))

check(mtn("diff", "--reverse", rev, rev), 1, false, true)
check(qgrep("'--reverse' only allowed with exactly one revision", "stderr"))

-- no --reverse option
check(mtn("diff", "--revision=" .. rev), 0, true, false)
check(qgrep("from \\[614d24f144edd2ef92ad9f8bc6d25bcf77e04101\\]", "stdout"))
check(qgrep("to \\[50812c5d2dcc96a92cea9c1c6ee7fd093774a4ea\\]", "stdout"))

check(mtn("diff", "--reverse", "--revision=" .. rev), 0, true, false)
check(qgrep("to \\[614d24f144edd2ef92ad9f8bc6d25bcf77e04101\\]", "stdout"))
check(qgrep("from \\[50812c5d2dcc96a92cea9c1c6ee7fd093774a4ea\\]", "stdout"))


-- end of file
