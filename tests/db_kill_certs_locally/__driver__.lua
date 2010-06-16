mtn_setup()

do
   local num = 0
   function commit_one(branch)
      num = num + 1
      addfile("file_" .. num, num)
      commit(branch)
      return base_revision()
   end
end

-- rename a branch

rev1 = commit_one("somebranch")
rev2 = commit_one("somebranch")
rev3 = commit_one("testbranch")

check(mtn("ls", "branches"), 0, true)
check(qgrep("somebranch", "stdout"))
check(qgrep("testbranch", "stdout"))

check(mtn("db", "set_epoch", "somebranch", string.rep("1234567890",4)))
check(mtn("ls", "epochs"), 0, true)
check(qgrep("somebranch", "stdout"))

check(mtn("cert", "b:somebranch", "branch", "otherbranch"), 0, nil, false)

check(mtn("ls", "branches"), 0, true)
check(qgrep("somebranch", "stdout"))
check(qgrep("otherbranch", "stdout"))
check(qgrep("testbranch", "stdout"))

check(mtn("heads", "-b", "otherbranch"), 0, true, false)
check(qgrep(rev2, "stdout"))

check(mtn("db", "kill_certs_locally", "i:", "branch", "somebranch"), 0, nil, false)
check(mtn("ls", "epochs"), 0, true)
check(not qgrep("somebranch", "stdout"))

check(mtn("ls", "branches"), 0, true)
check(not qgrep("somebranch", "stdout"))
check(qgrep("otherbranch", "stdout"))
check(qgrep("testbranch", "stdout"))


-- delete some tags

check(mtn("ls", "tags"))
check(mtn("tag", rev1, "sometag"))
check(mtn("tag", rev2, "sometag"))

check(mtn("ls", "tags"), 0, true)
check(qgrep(rev1:sub(0,10), "stdout"))
check(qgrep(rev2:sub(0,10), "stdout"))

check(mtn("db", "kill_certs_locally", "t:*", "tag"), 0, nil, false)
check(mtn("ls", "tags"))

-- check that branch heads get handled correctly
check(mtn("heads", "-b", "otherbranch"), 0, true, false)
check(qgrep(rev2, "stdout"))
check(mtn("db", "kill_certs_locally", "h:otherbranch", "branch", "otherbranch"), 0, nil, false)
check(mtn("heads", "-b", "otherbranch"), 0, true, false)
check(not qgrep(rev2, "stdout"))
check(qgrep(rev1, "stdout"))