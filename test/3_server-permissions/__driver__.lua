-- server permissions
--   check that write_permissions works properly

admin = new_person("admin")
developer = new_person("developer")
evilguy = new_person("evilguy")
user = new_person("user")

server = new_person("server")
server:fetch_keys(admin, developer, evilguy, user)

get("server-policy", server.confdir.."/policy")

-- setup policy branch

admin:push_to(server)


-- overly permissive right now
evilguy:push_to(server)


-- fix policy branch
admin:push_to(server)
evilguy:push_to(server)
