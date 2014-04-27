includecommon("netsync.lua")
mtn_setup()
netsync.setup()

srv2 = netsync.start(2)
srv3 = netsync.start(3)

srv2:sync("foo", 1)
srv3:sync("bar", 1)

revs = {}
revs.foo = {}
revs.bar = {}
revs.baz = {}

check(mtn("setup", "-bfoo", "foo"))
writefile("foo/foo", "foo")
check(indir("foo", mtn("add", "foo")), 0, nil, false)

check(mtn("setup", "-bbar", "bar"))
writefile("bar/bar", "bar")
check(indir("bar", mtn("add", "bar")), 0, nil, false)

check(mtn("setup", "-bbaz", "baz"))
writefile("baz/baz", "baz")
check(indir("baz", mtn("add", "baz")), 0, nil, false)

function base(branch)
  local workrev = readfile(branch .. "/_MTN/revision")
  local extract = string.gsub(workrev, "^.*old_revision %[(%x*)%].*$", "%1")
  if extract == workrev then
    err("failed to extract base revision from _MTN/revision")
  end
  return extract
end

function commit_something(branch)
   writefile(branch.."/"..branch, branch .. " " .. base(branch))
   check(indir(branch, mtn("ci", "-mx")), 0, false, false)
   local newbase = base(branch)
   table.insert(revs[branch], newbase)
   return newbase
end

-- verify that the given server has exactly the listed revisions
function check_remote_revisions(srv, list)
   check(mtn("automate", "remote", "--remote-stdio-host", srv.address,
	     "select", "i:"), 0, true, false)
   local ret = open_or_err("stdout")
   local n = 0
   for line in ret:lines() do
      n = n + 1
      local found = false
      for _, x in ipairs(list) do
	 if x == line then found = true end
      end
      check(found)
   end
   local want = 0
   for _, x in ipairs(list) do
      want = want + 1
   end
   check(n == want)
   ret:close()
end

for i = 1,2 do
   commit_something("foo")
   commit_something("bar")
   commit_something("baz")

   check(mtn("sync", srv2.address), 0, nil, false)
   check(mtn("sync", srv3.address), 0, nil, false)

   check_remote_revisions(srv2, revs.foo)
   check_remote_revisions(srv3, revs.bar)
end

-- check that pushing something different doesn't change the default

check(mtn("sync", srv3.address, "baz"), 0, nil, false)
barbaz = {}
for _, x in ipairs(revs.bar) do
   table.insert(barbaz, x)
end
for _, x in ipairs(revs.baz) do
   table.insert(barbaz, x)
end
check_remote_revisions(srv3, barbaz)
commit_something("bar")
commit_something("baz")
table.insert(barbaz, base("bar"))
check(mtn("sync", srv3.address), 0, nil, false)
check_remote_revisions(srv3, barbaz)

-- check that --set-default works
check(mtn("sync", srv2.address, "bar", "--set-default"), 0, nil, false)
foobar = {}
for _, x in ipairs(revs.foo) do
   table.insert(foobar, x)
end
for _, x in ipairs(revs.bar) do
   table.insert(foobar, x)
end
check_remote_revisions(srv2, foobar)
commit_something("foo")
commit_something("bar")
table.insert(foobar, base("bar"))
check(mtn("sync", srv2.address), 0, nil, false)
check_remote_revisions(srv2, foobar)
