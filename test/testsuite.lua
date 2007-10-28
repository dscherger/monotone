
math.randomseed(get_pid())
testdir = srcdir

function start_server(server)
   local srv = bg(server:run("--bind="..server.address, "serve"),
		  false, false, false)

  -- wait for "beginning service..."
  while fsize(srv.prefix .. "stderr") == 0 do
    sleep(1)
    check(not srv:wait(0))
  end
  return srv
end
function run_netsync(what, client, server, result, ...)
  set_env('MTN_SERVER_ADDR', server.address)
  srv = start_server(server)
  local t = arg
  if #t == 0 then
    t = {"*"}
  end

  check(client:run(what, server.address, unpack(t)), result, false, false)
  sleep(1)
  srv:finish()
end

function setup_confdir(dest)
   -- Remember, srcdir is where this file is, which is not the
   -- project root directory.
   check(copy(srcdir.."/../policy.lua", dest.."/policy.lua"))
   check(copy(srcdir.."/../update-policy.lua", dest.."/update-policy.lua"))
   check(copy(srcdir.."/../update-policy.sh", dest.."/update-policy.sh"))
   check({"chmod", "+x", dest.."/update-policy.sh"})
   check(copy(testdir.."/monotonerc", dest.."/monotonerc"))
   check(copy(testdir.."/read-permissions", dest.."/read-permissions"))
   check(copy(testdir.."/write-permissions", dest.."/write-permissions"))
end

function new_workspace(person, dir)
   local workspace = {}
   local mt = {}
   mt.__index = mt

   workspace.owner = person
   workspace.dir = dir

   mt.fullpath = function(obj, path)
		    return obj.owner:fullpath(obj.dir).."/"..path
		 end
   mt.run = function(obj, ...)
	       return obj.owner:runin(obj.dir, unpack(arg))
	    end
   mt.commit = function(obj, comment)
		  if comment == nil then
		     comment = "no comment"
		  end
		  check(obj:run("commit", "-m", comment), 0, false, false)
	       end
   mt.drop = function(obj, ...)
		check(obj:run("drop", "-R", unpack(arg)), 0, false, false)
	     end
   mt.adddir = function(obj, dirname)
		  mkdir(obj:fullpath(dirname))
		  check(obj:run("add", dirname), 0, false, false)
	       end
   mt.addfile = function(obj, filename, contents)
		   writefile(obj:fullpath(filename), contents)
		   check(obj:run("add", filename), 0, false, false)
		end
   mt.editfile = function(obj, filename, contents)
		    writefile(obj:fullpath(filename), contents)
		 end
   mt.readfile = function(obj, filename)
		    return readfile(obj:fullpath(filename))
		 end

   return setmetatable(workspace, mt)
end

function new_person(name)
   local person = {}
   local mt = {}
   mt.__index = mt

   person.name = name
   person.basedir = test.root.."/"..name
   person.confdir = person.basedir.."/conf"
   person.keydir = person.confdir.."/keys"
   person.db = person.basedir.."/test.mtn"
   mkdir(person.basedir)
   mkdir(person.confdir)
   mkdir(person.keydir)
   person.address = "localhost:" .. math.random(1024, 65535)

   setup_confdir(person.confdir)

   mt.fullpath = function(obj, path)
		    return obj.basedir.."/"..path
		 end
   mt.runin = function(obj, dir, ...)
		 return indir(obj:fullpath(dir),
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
   mt.setup = function(obj, branch)
		 check(obj:run("setup", branch, "-b", branch), 0, false, false)
		 return new_workspace(obj, branch)
	      end
   mt.checkout = function(obj, branch)
		    check(obj:run("checkout", branch, "-b", branch), 0, false, false)
		    return new_workspace(obj, branch)
		 end
   mt.pubkey = function(obj)
		  check(obj:run("pubkey", obj.name), 0, true, false)
		  return readfile("stdout")
	       end
   mt.read = function(obj, what)
		check(obj:run("read"), 0, false, false, what)
	     end
   mt.fetch_keys = function(obj, ...)
		      for i,x in ipairs(arg) do
			 obj:read(x:pubkey())
		      end
		   end
  mt.update_policy = function(obj)
                        local srv = start_server(obj)
                        check({obj.confdir .. "/update-policy.sh",
                               "-fg", obj.confdir,
                               "-server", obj.address}, 0, false, false)
                        srv:finish()
                     end

   person = setmetatable(person, mt)
   check(person:run("db", "init"), 0, false, false)
   check(person:run("genkey", person.name), 0, false, false, string.rep(person.name.."\n", 2))
   return person
end
