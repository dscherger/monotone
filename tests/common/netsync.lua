-- Conveniently, all network tests load this file, so this skip_if
-- suffices to skip all network tests if the network is unavailable
-- (see lua-testsuite.lua and [platform]/tester-check-net.c).
skip_if(no_network_tests)

function mtn2(...)
  return mtn("--db=test2.db", "--keydir=keys2", ...)
end

function mtn3(...)
  return mtn("--db=test3.db", "--keydir=keys3", ...)
end

netsync = {}
netsync.internal = {}

function netsync.setup()
  check(copy("test.db", "test2.db"))
  check(copy("keys", "keys2"))
  check(copy("test.db", "test3.db"))
  check(copy("keys", "keys3"))
  check(getstd("common/netsync-hooks.lua", "netsync.lua"))
  math.randomseed(get_pid())
end

function netsync.setup_with_notes()
  netsync.setup()
  check(getstd("common/netsync-hooks_with_notes.lua", "netsync.lua"))
end

function netsync.internal.client(srv, oper, pat, n, res, save_output)
   if n == nil then n = 2 end
   if n == 1 then
      args = {"--rcfile=netsync.lua", "--keydir=keys",
	      "--db=test.db", oper, srv.address}
   else
      args = {"--rcfile=netsync.lua", "--keydir=keys"..n,
	      "--db=test"..n..".db", oper, srv.address}
   end
   if type(pat) == "string" then
      table.insert(args, pat)
   elseif type(pat) == "table" then
      for k, v in pairs(pat) do
	 table.insert(args, v)
      end
   elseif pat ~= nil then
      err("Bad pattern type "..type(pat))
   end
   if save_output == nil then
      save_output = false
   end
   check(mtn(unpack(args)), res, save_output, save_output)
end
function netsync.internal.pull(srv, pat, n, res, save_output)
   srv:client("pull", pat, n, res, save_output)
end
function netsync.internal.push(srv, pat, n, res, save_output)
   srv:client("push", pat, n, res, save_output)
end
function netsync.internal.sync(srv, pat, n, res, save_output)
   srv:client("sync", pat, n, res, save_output)
end

function netsync.start(opts, n, min)
  if type(opts) == "number" then
    min = n
    n = opts
    opts = nil
  end
  local args = {}
  local fn = mtn
  local addr = "localhost:" .. math.random(1024, 65535)
  table.insert(args, "--dump=_MTN/server_dump")
  table.insert(args, "--bind="..addr)
  if min then
    fn = minhooks_mtn
  else
    table.insert(args, "--rcfile=netsync.lua")
  end
  if n ~= nil then
    table.insert(args, "--keydir=keys"..n)
    table.insert(args, "--db=test"..n..".db")
  end
  table.insert(args, "serve")
  if type(opts) == "table" then
    for k, v in pairs(opts) do
      table.insert(args, v)
    end
  elseif type(opts) ~= "nil" then
    err("netsync.start wants a table, not a "..type(opts).." as a first argument")
  end
  local argv = fn(unpack(args))
  local out = bg(argv, false, false, false)
  out.address = addr
  out.url = "mtn://" .. addr
  out.argv = argv
  local mt = getmetatable(out)
  mt.client = netsync.internal.client
  mt.pull = netsync.internal.pull
  mt.push = netsync.internal.push
  mt.sync = netsync.internal.sync
  mt.restart = function(obj)
		  local newobj = bg(obj.argv, false, false, false)
		  for x,y in pairs(newobj) do
		     obj[x] = y
		  end
		  -- wait for "beginning service..."
		  while fsize(obj.prefix .. "stderr") == 0 do
		     sleep(1)
		     check(out:check())
		  end
	       end
  local mt_wait = mt.wait
  mt.check = function(obj) return not mt_wait(obj, 0) end
  mt.wait = function(obj, timeout)
	       if timeout == nil then
		  timeout = 5
		  L(locheader(), "You really should give an argument to server.wait()\n")
	       end
	       if type(timeout) ~= "number" then
		  err("Bad timeout of type "..type(timeout))
	       end
	       return mt_wait(obj, timeout)
	    end
  -- wait for "beginning service..."
  while fsize(out.prefix .. "stderr") == 0 do
    sleep(1)
    check(out:check())
 end
 local mt_finish = mt.finish
 mt.finish = function(obj, timeout)
                sleep(1)
                mt_finish(obj, timeout)
             end
 mt.stop = mt.finish
 return out
end

function netsync.internal.run(oper, pat, opts)
  local srv = netsync.start(opts)
  if type(opts) == "table" then
    if type(pat) ~= "table" then
       err("first argument to netsync."..oper.." should be a table when second argument is present")
    end
    for k, v in pairs(opts) do
      table.insert(pat, v)
    end
  elseif type(opts) ~= "nil" then
    err("second argument to netsync."..oper.." should be a table")
  end
  srv:client(oper, pat)
  srv:finish()
end

function netsync.pull(pat, opts) netsync.internal.run("pull", pat, opts) end
function netsync.push(pat, opts) netsync.internal.run("push", pat, opts) end
function netsync.sync(pat, opts) netsync.internal.run("sync", pat, opts) end
