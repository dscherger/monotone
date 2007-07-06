-- utility functions, could be moved to std_hooks.lua
function read_conffile(name)
   return read_contents_of_file(get_confdir() .. "/" .. name, "r")
end

function read_basic_io_conffile(name)
   local dat = read_conffile(name)
   if dat == nil then return nil, false end
   return parse_basic_io(dat), true
end

function conffile_iterator(name)
   local out = {}
   out.file = io.open(get_confdir() .. "/" .. name, "r")
   if out.file == nil then return nil, false end
   local mt = {}
   mt.__index = mt
   mt.get = function()
	       if out.file == nil then return nil end
	       out.line = out.file:read()
	       return out.line
	    end
   mt.close = function()
		 if out.file == nil then return end
		 io.close(out.file)
	      end
   return setmetatable(out, mt), true
end

function trim(str)
   local _,_,r = string.find(str, "%s*([^%s]*)%s*")
   return r
end

------------------------------------------------------------------------

function branch_in_prefix(branch, prefix)
   if branch == prefix then
      return true
   end
   if string.sub(branch, 1, string.len(prefix)+1) == prefix .. '.' then
      return true
   end
   return false
end

do
   local old_write_permitted = get_netsync_write_permitted
   function get_netsync_write_permitted(ident)
      local committers, ok
      committers, exists = conffile_iterator("policy/override-write-permissions")
      if not exists then
	 committers = conffile_iterator("policy/cache/write-permissions")
      end
      if committers ~= nil then
	 while committers:get() do
	    if globish_match(trim(committers.line), ident) then
	       committers:close()
	       return true
	    end
	 end
	 committers:close()
      end
      return old_write_permitted(ident)
   end
end

function push_to_other_servers(triggerkey, branches)
   local data, exists
   data, exists = read_basic_io_conffile("policy/override-all-servers")
   if not exists then
      data, exists = read_basic_io_conffile("policy/cache/all-servers")
   end
   if not exists then return end
   if data == nil then
      io.stderr:write("server list file cannot be parsed\n")
      return
   end

   -- If a branch was received from a server that's in our distribution
   -- list for that branch, don't forward it.
   for local i, item in pairs(data)
   do
      if item.name == "server" then
	 if item.values[2] == triggerkey
	 then
	    local dropprefix = item.values[3]
	    for local branch, _ in pairs(branches) do
	       if branch_in_prefix(branch, dropprefix) then
		  branches[branch] = false
	       end
	    end
	 end
      end
   end

   -- Build the list of what servers get what branches.
   local pushto = {}
   for i, item in pairs(data) do
      if item.name == "server" then
	 local srvname = item.values[1]
	 local prefix = item.values[3]
	 for local branch, inc in pairs(branches) do
	    if inc and branch_in_prefix(branch, prefix) then
	       if pushto[srvname] == nil then
		  pushto[srvname] = {}
	       end
	       pushto[srvname][branch] = true
	    end
	 end
      end
   end

   -- Send thing where they go.
   for local server, what in pairs(data) do
      local include = "{"
      local first = true
      for local b, _ in pairs(what) do
	 include = include .. b
	 if not first then
	    include = include .. ","
	 end
	 first = false
      end
      include = include .. "}"
      server_request_sync("sync", server, include, "")
   end
end


function note_netsync_start(sid, role, what, rhost, rkey, include, exclude)
   if sessions == nil then sessions = {} end
   sessions[sid] = {
      key = rkey,
      branches = {},
      include = include,
      exclude = exclude
   }
end

function note_netsync_revision_received(rid, rdat, certs, sid)
   for _, cert in pairs (certs) do
      if cert.name == "branch" then
	 sessions[sid].branches[cert.value] = true
      end
   end
end

function note_netsync_cert_received(rid, key, name, value, sid)
   if name == "branch" then
      sessions[sid].branches[value] = true
   end
end

function note_netsync_end(sid, status, bi, bo, ci, co, ri, ro, ki, ko)
   if ci > 0 or ri > 0 or ki > 0 then
      server_maybe_request_sync(sessions[sid].key, sessions[sid].branches)
   elseif sessions[sid].include == '' and
          sessions[sid].exclude == 'policy-branches-updated' then
      log("resyncing after a config update...")
      server_maybe_request_sync('')
   end
   
   -- Do we update the control checkout?
   local ctlbranch = trim(read_conffile("serverctl-branch"))
   if ctlbranch and sessions[sid].branches[ctlbranch] then
      execute(get_confdir() .. "/update-policy.sh", get_confdir())
   end
   sessions[sid] = nil
end