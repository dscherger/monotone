-- hooks used when updating the policy branches
-- looks at the DELEGATIONS and PREFIX env vars

trusted_keys = {}
do
   local prefix = os.getenv('PREFIX')
   local delegations_file = os.getenv('DELEGATIONS')
   if prefix then
      local delegations, readable = read_basic_io_conffile(delegations_file)
      if delegations == nil then
        if readable then
          error("Cannot parse " .. delegations_file)
        else
          error("Cannot read " .. delegations_file)
        end
      end

      local myprefix = false
      for _, item in pairs(delegations) do
	 if item.name == 'delegate' then
	    if item.values[1] == prefix then
	       myprefix = true
	    else
	       myprefix = false
	    end
	 end
	 if item.name == 'admin' and myprefix then
	    table.insert(trusted_keys, item.values[1])
            io.stderr:write("Adding key "..item.values[1].." to trusted list\n")
	 end
      end
   else
      io.stderr:write("Prefix not known, cannot find trusted keys. Sorry.\n")
   end
end

function get_revision_cert_trust(signers, id, name, value)
   for _,key in pairs(trusted_keys) do
      for _, s in pairs(signers) do
	 if key == s then
	    return true
	 end
      end
   end
   return false
end

-- We don't want to re-trigger any events.
function note_netsync_start()
   return
end
function note_netsync_revision_received()
   return
end
function note_netsync_cert_received()
   return
end
function note_netsync_end()
   return
end
