-- test that 'mtn cert' does not require a branch
-- bug# 7221

mtn_setup()

addfile("base", "base")
commit()
rev = base_revision()

-- ensure we don't find a branch
remove("_MTN")

check(mtn("cert", rev, "branch", "anotherbranch"), 0, false, false)

-- end of file
