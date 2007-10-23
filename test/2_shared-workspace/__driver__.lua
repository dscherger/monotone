-- shared workspace

admin = new_person("admin")
developer = new_person("developer")
evilguy = new_person("evilguy")
user = new_person("user")

server = new_person("server")
server:fetch_keys(admin, developer, evilguy, user)

get("user-policy", user.confdir.."/policy")

-- The update script needs to be able to work from a netsync client,
-- instead of just the server.
xfail_if(true, false)
