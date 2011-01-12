-- Lua snippet to display what branches were affected by revisions and certs
-- that came into the database.  I integrate it into my ~/.monotone/monotonerc
-- /Richard Levitte
--
-- Released as public domain

do
   netsync_branches = {}

   function RL_note_netsync_cert_received(rev_id,key,name,value,nonce)
      if name == "branch" then
	 if netsync_branches[nonce][value] == nil then
	    netsync_branches[nonce][value] = 1
	 else
	    netsync_branches[nonce][value] = netsync_branches[nonce][value] + 1
	 end
      end
   end

   notifier = {
      start =
	 function(session_id,...)
	    netsync_branches[session_id] = {}
	    return "continue",nil
	 end,
      revision_received =
	 function(new_id,revision,certs,session_id)
	    for _, item in pairs(certs) do
	       RL_note_netsync_cert_received(new_id,
					     item.key,
					     item.name,
					     item.value,
					     session_id)
	    end
	    return "continue",nil
	 end,
      cert_received =
	 function(rev_id,key,name,value,session_id)
	    RL_note_netsync_cert_received(rev_id,
					  key,name,value,
					  session_id)
	    return "continue",nil
	 end,
      ["end"] = function(session_id)
		   local first = true
		   for item, amount in pairs(netsync_branches[session_id])
		   do
		      if first then
			 io.stderr:write("Affected branches:\n")   
			 first = false
		      end
		      io.stderr:write("  ",item,"  (",amount,")\n")   
		   end
		   netsync_branches[session_id] = nil
		   return "continue",nil
		end
   }

   local v,m = push_hook_functions(notifier)
   if not v then
      error(m)
   elseif m then
      io.stderr:write("Warning: ",m,"\n")
   end
end
