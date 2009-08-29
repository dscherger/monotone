include("common/netsync.lua")

mtn_setup()
netsync.setup()


function mk_old(ver)
   local exe = "mtn-netsync" .. ver
   skip_if(not existsonpath(exe))
   local rcfile
   if get("netsync"..ver..".lua") then
      rcfile = "netsync"..ver..".lua"
   else
      rcfile = "test_hooks.lua"
   end
   local fn = function (...)
                 return {exe, "--norc", "--root=.",
                    "--confdir=.",
                    "--rcfile="..rcfile,
                    "--key=tester@test.net",
                    "--db=test"..ver..".mtn",
                    "--keydir=keys",
                    unpack(arg)}
              end

   check(getstd("test_keys"))
   check(fn("read", "test_keys"), 0, nil, false)
   remove("test_keys")
   return {fn = fn,
      reset = function ()
                 check(remove("test"..ver..".mtn"))
                 check(fn("db", "init"))
              end
   }
end

current = {fn = mtn, reset = function ()
                                check(remove("test.db"))
                                check(mtn("db", "init"))
                             end
}

version6 = mk_old(6)

function do_commits(my_mtn, dat)
   check(remove("_MTN"))
   check(my_mtn("setup", ".", "--branch=testbranch"))
   check(writefile("base", "base"))
   check(my_mtn("add", "base"), 0, nil, false)
   check(my_mtn("commit", "--message=base commit"), 0, nil, false)

   check(writefile("child", dat))
   check(my_mtn("add", "child"), 0, nil, false)
   check(my_mtn("commit", "--message=child commit"), 0, nil, false)
end

function check_pair(client, server)
   client.reset()
   do_commits(client.fn, "client")
   server.reset()
   do_commits(server.fn, "server")

   local addr = "localhost:" .. math.random(1024, 65535)
   local srv = bg(server.fn("serve", "--rcfile=netsync.lua", "--bind="..addr),
                  false, false, false)
   check(client.fn("sync", addr, "testbranch"), 0, false, false, false)
   sleep(1)
   srv:finish()
end

check_pair(current, version6)
check_pair(version6, current)
