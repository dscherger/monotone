-- basic_workspace

admin = new_person("admin")
developer = new_person("developer")
evilguy = new_person("evilguy")
user = new_person("user")

server = new_person("server")
server:fetch_keys(admin, developer, evilguy, user)


-- Updating a workspace should validate candidate revisions
-- against the policy.

mkdir(user.confdir.."/policy")
writefile(user.confdir.."/policy/write-permissions", "admin\ndeveloper\n")

-- Need something to start with...
dev_ws = developer:setup("test.branch")
dev_ws:addfile("version", "1")
dev_ws:commit("Initial commit")
developer:push_to(server)

user:pull_from(server)
user_ws = user:checkout("test.branch")

-- Check an allowed update...
dev_ws:editfile("version", "2")
dev_ws:commit("bugfix")
developer:push_to(server)
user:pull_from(server)
check(user_ws:run("update"), 0, false, false)
check(user_ws:readfile("version") == "2")

-- Check a disallowed update...
evilguy:pull_from(server)
evil_ws = evilguy:checkout("test.branch")
evil_ws:editfile("version", "3")
evil_ws:commit("add backdoor")
evilguy:push_to(server)

user:pull_from(server)
check(user_ws:run("update"), 0, false, false)
check(user_ws:readfile("version") == "2")

-- More complex

--policy_ws = admin.setup("policy-branch")
--policy_ws.add_file("write_permissions", "admin\ndeveloper\n")
--admin.push_to(server)