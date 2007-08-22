
math.randomseed(get_pid())
testdir = srcdir.."/test"

function run_netsync(what, client, server, result, ...)
   local srv = bg(server.run("--bind="..server.address, "serve"),
		  false, false, false)

  -- wait for "beginning service..."
  while fsize(srv.prefix .. "stderr") == 0 do
    sleep(1)
    check(not srv:wait(0))
 end

 check(client.run("pull", server.address, unpack(arg)), result, false, false)
 srv:stop()
end

function setup_confdir(dest)
   copy(srcdir.."/policy.lua", dest.."/policy.lua")
   copy(srcdir.."/update-policy.lua", dest.."/update-policy.lua")
   copy(srcdir.."/update-policy.sh", dest.."/update-policy.sh")
   copy(testdir.."/monotonerc", dest.."/monotonerc")
end

function new_person(name)
   local person = {}
   local mt = {}
   mt.__index = mt

   person.basedir = test.root.."/"..name
   person.confdir = person.basedir.."/conf"
   person.keydir = person.confdir.."/keys"
   person.db = person.basedir.."/test.mtn"
   mkdir(person.basedir)
   mkdir(person.confdir)
   mkdir(person.keydir)
   person.address = "localhost:" .. math.random(1024, 65535)

   setup_confdir(person.confdir)

   mt.runin = function(obj, dir, ...)
	       return indir(obj.basedir.."/"..dir,
			    {"mtn",
			       "--root="..obj.basedir,
			       "--confdir="..obj.confdir,
			       "--keydir="..obj.keydir,
			       "--db="..obj.db,
			       unpack(arg)})
		 end
   mt.run = function(obj, ...)
	       return obj:runin("", unpack(arg))
	    end

   mt.push_to = function(...)
		   run_netsync("push", unpack(arg))
		end
   mt.pull_from = function(...)
		     run_netsync("pull", unpack(arg))
		  end
   mt.sync_with = function(...)
		     run_netsync("sync", unpack(arg))
		  end

   return setmetatable(person, mt)
end
