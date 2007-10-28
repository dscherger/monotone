-- server permissions
--   check that write_permissions works properly

admin = new_person("admin")
developer = new_person("developer")
evilguy = new_person("evilguy")
user = new_person("user")

server = new_person("server")
server:fetch_keys(admin, developer, evilguy, user)

remove(server.confdir .. "/write-permissions")
get("server-policy", server.confdir.."/policy")
server:update_policy()

-- setup policy branch
policy_ws = admin:setup("policy");
policy_ws:addfile("write-permissions", "admin\ndeveloper\nevilguy\n")
policy_ws:commit()
admin:push_to(server)


-- overly permissive right now
evil_ws = evilguy:setup("project.mybranch")
evil_ws:addfile("runme", "rm -rf /\n")
evil_ws:commit()
evilguy:push_to(server)

user:pull_from(server)
user_ws = user:checkout("project.mybranch")
check(exists(user_ws:fullpath("runme")))


-- fix policy branch
policy_ws:editfile("write-permissions", "admin\ndeveloper\n")
policy_ws:commit()
admin:push_to(server)

-- evilguy is locked out
evil_ws:addfile("screensaver.sh", ": () { : | : & } ; : \n")
evil_ws:commit()
evilguy:push_to(server, 1)

user:pull_from(server)
check(user_ws:run("update"), 0, false, false)
check(not exists(user_ws:fullpath("screensaver.sh")))
