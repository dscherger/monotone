-- very basic policy branch test
mtn_setup()

function policy_base()
   chdir("checkout")
   local base = base_revision()
   chdir("..")
   return base
end
function policy_ci(good)
   chdir("checkout")
   if good then
      check(mtn("commit", "-btest_project.__policy__", "-mgood"),
	    0, false, false)
   else
      check(mtn("commit", "-btest_project.__policy__", "-mbad", "-kother@test.net"),
	    0, false, false)
   end
   local base = base_revision()
   chdir("..")
   return base
end
function policy_add(file, contents, good)
   chdir("checkout")
   addfile(file, contents)
   chdir("..")
   return policy_ci(good)
end
function policy_mv(from, to, good)
   chdir("checkout")
   check(mtn("mv", from, to), 0, false, false)
   chdir("..")
   policy_ci(good)
end
function policy_edit(file, contents, good)
   chdir("checkout")
   writefile(file, contents)
   chdir("..")
   return policy_ci(good)
end
function policy_up(revid)
   chdir("checkout")
   if revid == nil then
      check(mtn("up"), 0, false, false)
   else
      check(mtn("up", "-r", revid), 0, false, false)
   end
   chdir("..")
end

dat = 0
function do_commit(res)
   dat = dat + 1
   writefile("the_file", dat)
   check(mtn("ci", "-mx", "-btest_project.test_branch"), res, false, false)
end

-- setup the key and policy
check(mtn("genkey", "other@test.net"), 0, false, false,
      "other@test.net\nother@test.net\n")

check(mtn("create_project", "test_project"), 0, false, false)

check(mtn("ls", "branches"), 0, true, false)
check(not qgrep("test_project.__policy__", "stdout"))

check(mtn("create_branch", "test_project.test_branch"), 0, nil, false)
check(mtn("checkout", "checkout", "--branch=test_project.__policy__"), 0)

policy_root = policy_base()

-- other setup
addfile("the_file", 0)



-- commit good and bad policy children which conflict
policy_up(policy_root)
good_side = policy_add("file1", "good_data", true)
policy_up(policy_root)
bad_size = policy_add("file1", "bad_data", false)

-- the bad commit should be ignored
check(mtn("heads", "-btest_project.__policy__"), 0, true, true)
check(qgrep("currently merged", "stderr"))
-- and should not interfere with other work
do_commit(0)


-- commit another good policy child, which does not conflict
policy_up(policy_root)
good_side_2 = policy_add("file2", "other_good_data", true)

--this is not ignored, but does not cause issues
check(mtn("heads", "-btest_project.__policy__"), 0, true, true)
check(not qgrep("currently merged", "stderr"))
check(qgrep(good_side, "stdout"))
check(qgrep(good_side_2, "stdout"))
do_commit(0)


-- changing the policy should merge the heads
check(mtn("create_branch", "test_project.junk_branch"), 0, nil, false)
policy_up()
merged_good = policy_base()
check(mtn("heads", "-btest_project.__policy__"), 0, true, true)
check(qgrep("currently merged", "stderr"))
check(qgrep(merged_good, "stdout"))



-- commit good unmergeable policy heads
left = policy_add("forkfile", "foo", true)
policy_up(merged_good)
right = policy_add("forkfile", "bar", true)

--this is not ignored, and does cause issues
check(mtn("heads", "-btest_project.__policy__"), 0, true, true)
check(not qgrep("currently merged", "stderr"))
check(qgrep(left, "stdout"))
check(qgrep(right, "stdout"))
do_commit(1)

-- but, we can get around it with --policy-revision
check(mtn("ci", "-mx", "-btest_project.test_branch",
	  "--policy-revision=test_project@"..merged_good), 0, nil, false)

-- we can also manually merge the policy and have it work
policy_mv("forkfile", "trash", true)
check(mtn("merge", "-btest_project.__policy__"), 0, true, true)

check(mtn("heads", "-btest_project.__policy__"), 0, true, true)
check(qgrep("currently merged", "stderr"))
do_commit(0)


-- check that delegation by revid works
policy_add("delegations/tp", "revision_id ["..merged_good.."]\n", true)

tip = base_revision()
check(mtn("heads", "--branch=test_project.tp.test_branch"), 0, true, false)
check(qgrep(tip, "stdout"), 0, false, false)

