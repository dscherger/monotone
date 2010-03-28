
mtn_setup()

check(mtn("create_project", "test_project"), 0, false, false)

check(mtn("create_subpolicy", "test_project.subproject"), 0, false, false)

check(mtn("create_subpolicy", "test_project.subproject.subsub"), 0, false, false)


check(mtn("checkout", "checkout", "--branch=test_project.subproject.__policy__"), 0)

check(exists("checkout/delegations/subsub"))

function require_parent_is(parent_policy)
   check(qgrep("Parent policy is '"..parent_policy.."'", "stderr"))
end

policies = "test_project\n"
policies = policies .. "test_project.subproject\n"
policies = policies .. "test_project.subproject.subsub\n"
check(mtn("ls", "policies", "-R"), 0, policies)

check(mtn("create_branch", "test_project.firstbranch"), 0, false, true)
require_parent_is("test_project")

check(mtn("create_branch", "test_project.subproject.secondbranch"), 0, false, true)
require_parent_is("test_project.subproject")

check(mtn("create_branch", "test_project.subproject.subsub.thirdbranch"), 0, false, true)
require_parent_is("test_project.subproject.subsub")

check(mtn("ls", "branches"), 0, true)

check(qgrep("test_project.firstbranch", "stdout"))
check(qgrep("test_project.subproject.secondbranch", "stdout"))
check(qgrep("test_project.subproject.subsub.thirdbranch", "stdout"))

branchname = "test_project.subproject.secondbranch"
check(mtn("heads", "-b", branchname), 0, false, true)
check(qgrep("is empty", "stderr"))

check(mtn("setup", "secondbranch", "-b", branchname))
check(writefile("secondbranch/testfile", "file contents\n"))
check(indir("secondbranch", mtn("add", "testfile")), 0, false, false)
check(indir("secondbranch", mtn("commit", "-mx")), 0, false, false)
