include("common/netsync.lua")

mtn_setup()
netsync.setup()


function make_old(ver)
   local exe = "mtn-netsync" .. ver
   if not existsonpath(exe) then
      return nil
   end
   local rcfile
   if get("netsync"..ver..".lua") then
      rcfile = "netsync"..ver..".lua"
   else
      rcfile = "test_hooks.lua"
   end
   local nostd = "--no-standard-rcfiles"
   if ver < 8 then
      nostd = "--nostd"
   end
   local fn = function (...)
                 return {exe, nostd, "--root=.",
                    "--confdir=.",
                    "--rcfile="..rcfile,
                    "--key=tester@test.net",
                    "--db=test"..ver..".mtn",
                    "--keydir=keys",
                    ...}
              end

   check(getstd("test_keys"))
   check(fn("read", "test_keys"), 0, nil, false)
   remove("test_keys")
   return {fn = fn, net_fn = fn,
      reset = function ()
                 check(remove("test"..ver..".mtn"))
                 check(remove("test"..ver..".mtn-journal"))
                 check(fn("db", "init"))
              end
   }
end

current = {fn = mtn, net_fn = mtn,
   reset = function ()
              check(remove("test.db"))
              check(remove("test.db-journal"))
              check(mtn("db", "init"))
           end
}

function make_fake(min_ver, max_ver)
   local fn = function (...)
                 return mtn("--db=test"..max_ver..".mtn", ...)
              end
   local net_fn = function (...)
                     return mtn("--min-netsync-version="..min_ver,
                                "--max-netsync-version="..max_ver,
                                "--db=test"..max_ver..".mtn",
                                ...)
                  end
   return {fn = fn, net_fn = net_fn,
      reset = function ()
                 check(remove("test"..max_ver..".mtn"))
                 check(remove("test"..max_ver..".mtn-journal"))
                 check(fn("db", "init"))
              end
   }
end

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

function check_same_revs(cmd1, cmd2)
   check(cmd1, 0, true, false)
   local data1 = {}
   for l in io.lines("stdout") do table.insert(data1, l) end
   check(cmd2, 0, true, false)
   local data2 = {}
   for l in io.lines("stdout") do table.insert(data2, l) end
   L("Command 1 has ", table.getn(data1), " lines.")
   L("Command 2 has ", table.getn(data2), " lines.")
   check(table.getn(data1) == table.getn(data2))
   for i = 1, table.getn(data1) do
      local hash_len = 40
      check(data1[i]:sub(1, hash_len) == data2[i]:sub(1, hash_len))
   end
end

function check_pair(client, server)
   -- setup database contents
   client.reset()
   do_commits(client.fn, "client")
   sleep(2) -- make sure that the date certs are different
   server.reset()
   do_commits(server.fn, "server")

   -- exchange data
   local addr = "localhost:" .. math.random(1024, 65535)
   local srv = bg(server.net_fn("serve", "--rcfile=netsync.lua", "--bind="..addr),
                  false, false, false)
   sleep(1)
   check(client.net_fn("sync", addr, "testbranch"), 0, false, false, false)
   sleep(1)
   srv:finish()

   -- check for correct/matching contents
   check_same_stdout(client.fn("automate", "select", "i:"),
                     server.fn("automate", "select", "i:"))
   check_same_revs(client.fn("heads", "--branch=testbranch"),
		   server.fn("heads", "--branch=testbranch"))
   -- there should be 13 certs - 4 per rev, plus an extra date cert
   -- on the common base revision
   check(client.fn("db", "info"), 0, true)
   check(qgrep("^ *certs .*: *13 *$", "stdout"))
   check(server.fn("db", "info"), 0, true)
   check(qgrep("^ *certs *: *13 *$", "stdout"))
end

function check_against(ver)
   local real_other = make_old(ver)
   if real_other ~= nil then
      check_pair(current, real_other)
      check_pair(real_other, current)
   end
   local fake_other = make_fake(ver, ver)
   check_pair(current, fake_other)
   check_pair(fake_other, current)
end

-- check against compatible versions, both with fake "old" peers,
-- and with real old peers if they're available
check_against(6)
check_against(7)

-- check against a fake far-future version
fake_future = make_fake(1, 250)
check_pair(current, fake_future)
check_pair(fake_future, current)
