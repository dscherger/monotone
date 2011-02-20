
includecommon("netsync.lua")
mtn_setup()

--
-- test get_netsync_client_key
--
netsync.setup()

writefile("foo", "bar")
check(mtn2("add", "foo"), 0, false, false)
check(mtn2("commit", "-mx"), 0, false, false)

check(mtn("genkey", "badkey@test.net"), 0,
      false, false, string.rep("badkey@test.net\n",2))

get("read-permissions")
get("client-hooks.lua")

srv = netsync.start(2)

-- We don't want the --key argument, so we have to do this ourselves.
function client(what, ret)
  args = {"--rcfile=netsync.lua", "--rcfile=test_hooks.lua",
     "--keydir=keys",
     "--db=test.db", srv.address,
     "--rcfile=client-hooks.lua",
     "--no-workspace",
     "*"}
  for k, v in pairs(args) do
     table.insert(what, v)
  end
  check(raw_mtn(unpack(what)), ret, false, false)
end

client({"push"}, 0)
client({"pull"}, 0)
client({"sync"}, 0)
client({"pull", "--key=badkey@test.net"}, 1)

srv:stop()

--
-- test get_netsync_server_key
--

get("server-hooks.lua")
-- we send a SIGTERM to the server process to stop it, so this is also
-- what we expect as (negated) return value
SIGTERM=15

function server(what, ret, exp_err)
  local addr = "localhost:" .. math.random(1024, 65535)
  args = {"--rcfile=test_hooks.lua",
     "--keydir=keys",
     "--db=test.db", "--bind=" .. addr,
     "--no-workspace",
     "serve"}
  for k, v in pairs(args) do
     table.insert(what, v)
  end
  srv = bg(raw_mtn(unpack(what)), ret, false, true)
  srv:finish(3)
  if exp_err ~= nil then
    check(qgrep(exp_err, "stderr"))
  end
end

server({}, 1, "you have multiple private keys")
server({"--rcfile", "server-hooks.lua"}, -SIGTERM, "beginning service on localhost")

