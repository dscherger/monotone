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


-- iterator for the prefixes delegated by the given policy
-- returns idx, prefix, policy_branch
function delegations(policy_dir)
  local function fn(delegations, pos)
    if delegations == nil then return nil end
    local idx, val = next(delegations, pos)
    if idx == nil then return nil end
    if val.name == "delegate" then
      return idx, val.values[1], val.values[2]
    end
    return fn(delegations, idx)
  end
  local delegations = read_basic_io_conffile(policy_dir .. "/delegations")
  return fn, delegations, nil
end

do -- Do the policies trust a given key / set of keys
  -- First, look for an override-write-permissions, shortest prefix first.
  -- Then, look for a write-permissions, longest prefix first.
  -- If neither exists, try the old hook or default to open.

  -- Does a particular writers file include one of the given keys?
  local function file_trusts_keys(file, signers)
    local iter = conffile_iterator(file)
    if iter == nil then
      io.stderr:write("File <"..file.."> says: nil\n")
      return nil
    end
    local retval = false
    while iter:get() do
       for _,s in pairs(signers) do
          if s == iter.line or s == "*" then
             retval = true
          end
       end
    end
    iter:close()
    local retstr = "OK" if not retval then retstr = "DENY" end
    io.stderr:write("File <"..file.."> says: "..retstr.."\n")
    return retval
  end

  local function subpolicy_trusts_keys(policy_dir, branch, signers)
    for _, subprefix, subpolicy in delegations(policy_dir) do
      local overrides = policy_dir .. '/delegations.d/overrides/' .. subprefix
      local checkout = policy_dir .. '/delegations.d/checkouts/' .. subprefix
      if branch == nil or branch_in_prefix(branch, subprefix) then
        local override_file = overrides .. '/override-write-permissions'
        local override = file_trusts_keys(override_file, signers)
        if override == true then return override end

        if override == nil then
          local sub = subpolicy_trusts_keys(checkout, branch, signers)
          if sub ~= nil then return sub end
        end
      end
    end

    local here = file_trusts_keys(policy_dir .. '/write-permissions', signers)
    return here
  end

  function policy_trusts_keys(branch, keys)
    local override = file_trusts_keys("override-write-permissions", keys)
    if override ~= nil then return override end
    return subpolicy_trusts_keys('policy', branch, keys)
  end
  function policy_trusts_key(key)
    return policy_trusts_keys(nil, {key})
  end
end

do -- get_netsync_write_permitted
   local old_write_permitted = get_netsync_write_permitted
   function get_netsync_write_permitted(ident)
      local policy = policy_trusts_key(ident)
      if policy == true then return true end
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
   for i, item in pairs(data)
   do
      if item.name == "server" then
	 if item.values[2] == triggerkey
	 then
	    local dropprefix = item.values[3]
	    for branch, _ in pairs(branches) do
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
	 for branch, inc in pairs(branches) do
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
   for server, what in pairs(data) do
      local include = "{"
      local first = true
      for b, _ in pairs(what) do
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

sessions = {}

function note_netsync_start(sid, role, what, rhost, rkey, include, exclude)
   --if sessions == nil then sessions = {} end
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
      push_to_other_servers(sessions[sid].key, sessions[sid].branches)
   elseif sessions[sid].include == '' and
          sessions[sid].exclude == 'policy-branches-updated' then
      io.stderr:write("resyncing after a config update...")
      server_maybe_request_sync('')
   end
   
   -- Do we have policy branches to update?
   local updated_a_policy = false
   local updated_policies = '{'

  local policies = {}
  local function note_delegated(policies, policy_dir)
    for _, prefix, policy in delegations(policy_dir) do
      table.insert(policies, policy)
      note_delegated(policies, policy_dir .. '/delegations.d/checkouts/' .. prefix)
    end
  end
  note_delegated(policies, "policy")

   for _,policy in pairs(policies) do
      for br, _ in pairs(sessions[sid].branches) do
	 if policy == br then
	    if updated_a_policy then
	       updated_policies = updated_policies .. ','
	    end
	    updated_policies = updated_policies .. br
	    updated_a_policy = true
	 end
      end
   end
   updated_policies = updated_policies .. '}'

   if updated_a_policy then
      execute(get_confdir() .. "/update-policy.sh", get_confdir(), updated_policies)
   end

   sessions[sid] = nil
end

do
   local old_trust_hook = get_revision_cert_trust
   function get_revision_cert_trust(signers, id, name, value)
      if name == 'branch' then
	 local trusted = policy_trusts_keys(value, signers)
	 if trusted ~= nil then
	    return trusted
	 end
      end
      if old_trust_hook then
	 return old_trust_hook(signers, id, name, value)
      else
	 return true
      end
   end
end
