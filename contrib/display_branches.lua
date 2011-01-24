-- Lua snippet to display what branches were affected by revisions and certs
-- that came into the database.  I integrate it into my ~/.monotone/monotonerc
-- /Richard Levitte
--
-- Released as public domain

do
   netsync_branches = {}

   function RL_note_netsync_cert_received(direction,rev_id,key,name,value,nonce)
      if name == "branch" then
	 if netsync_branches[direction][nonce][value] == nil then
	    netsync_branches[direction][nonce][value] = 1
	 else
	    netsync_branches[direction][nonce][value] =
	       netsync_branches[direction][nonce][value] + 1
	 end
      end
   end

   notifier = {
      ["start"] =
	 function(session_id,...)
	    netsync_branches["received"] = {}
	    netsync_branches["sent"] = {}
	    netsync_branches["received"][session_id] = {}
	    netsync_branches["sent"][session_id] = {}
	    return "continue",nil
	 end,
      ["revision_received"] =
	 function(new_id,revision,certs,session_id)
	    for _, item in pairs(certs) do
	       RL_note_netsync_cert_received("received",
					     new_id,
					     item.key,
					     item.name,
					     item.value,
					     session_id)
	    end
	    return "continue",nil
	 end,
      ["revision_sent"] =
	 function(new_id,revision,certs,session_id)
	    for _, item in pairs(certs) do
	       RL_note_netsync_cert_received("sent",
					     new_id,
					     item.key,
					     item.name,
					     item.value,
					     session_id)
	    end
	    return "continue",nil
	 end,
      ["cert_received"] =
	 function(rev_id,key,name,value,session_id)
	    RL_note_netsync_cert_received("received",
					  rev_id,
					  key,name,value,
					  session_id)
	    return "continue",nil
	 end,
      ["cert_sent"] =
	 function(rev_id,key,name,value,session_id)
	    RL_note_netsync_cert_received("sent",
					  rev_id,
					  key,name,value,
					  session_id)
	    return "continue",nil
	 end,
      ["end"] =
	 function(session_id,status)
	    -- only try to display results if we got
	    -- at least partial contents
	    if status > 211 then
	       return "continue",nil
	    end

	    local first = true
	    for item, amount in pairs(netsync_branches["received"][session_id])
	    do
	       if first then
		  io.stderr:write("Received data on branches:\n")
		  first = false
	       end
	       io.stderr:write("  ",item,"  (",amount,")\n")   
	    end
	    netsync_branches["received"][session_id] = nil

	    first = true
	    for item, amount in pairs(netsync_branches["sent"][session_id])
	    do
	       if first then
		  io.stderr:write("Sent data on branches:\n")
		  first = false
	       end
	       io.stderr:write("  ",item,"  (",amount,")\n")   
	    end
	    netsync_branches["sent"][session_id] = nil
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
