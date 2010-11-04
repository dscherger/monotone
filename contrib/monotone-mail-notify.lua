-- This script reads a file "notify" in the configuration directory
-- The format is like that of "read-permissions", but instead of key
-- names, the values for 'allow' and 'deny' entries must be real email
-- addresses.
-- Additionally, at the top before anything else, you may have the
-- following entries:
--	server		This must be the server address.  This entry
--			is needed for the .sh script.
--			Default value: "localhost" (see _server below)
--	from		The sender email address.
--			Default value: see _from below
--	keydir		The directory with keys.
--			Default value: get_confdir() .. "/keys"
--	key		A key identity.
--			Default value: (empty)
--	shellscript	The script that does the actual emailing.
--			If left empty, it means it's taken care of
--			throught other means.
--			Note: the script is spawned and then entirely
--			left to its own devices, so as not to let the
--			monotone server hang too long.
--			Default value: (empty)
--
-- This will splat out files in _base. Use the .sh script from
-- cron to process those files
--
-- Copyright (c) 2007, Matthew Sackman (matthew at wellquite dot org)
--                     LShift Ltd (http://www.lshift.net)
--                     Thomas Keller <me@thomaskeller.biz>
--                     Whoever wrote the function "get_netsync_read_permitted"
-- Copyright (c) 2010, Richard Levitte <richard@levitte.org)
-- License: GPLv2 or later

do
   _from = "monotone@my.domain.please.change.me"
   _server = "localhost"
   _base = "/var/spool/monotone/"
   _keydir = get_confdir() .. "/keys"
   _key = ""
   _shellscript = ""
   _shellscript_log = get_confdir() .. "/notify.log"
   _shellscript_errlog = get_confdir() .. "/notify.err"

   local function table_print(T)
      local done = {}
      local function tprint_r(T, prefix)
	 for k,v in pairs(T) do
	    print(prefix..tostring(k),'=',tostring(v))
	    if type(v) == 'table' then
	       if not done[v] then
		  done[v] = true
		  tprint_r(v, prefix.."  ")
	       end
	    end
	 end
      end
      done[T] = true
      tprint_r(T, "")
   end

   local function table_toarray(t)
      local t1 = {}
      for j, val in pairs(t) do
	 table.insert(t1, val)
      end
      return t1
   end

   local function parse_configuration()
      local notifyfile = io.open(get_confdir() .. "/notify", "r")
      if (notifyfile == nil) then return nil end
      local dat = notifyfile:read("*a")
      io.close(notifyfile)
      local res = parse_basic_io(dat)
      if res == nil then
	 io.stderr:write("file notify cannot be parsed\n")
	 return nil
      end
      --print("monotone-mail-notify: BEGIN Parsed configuration")
      --table.print(res)
      --print("monotone-mail-notify: END Parsed configuration")
      return res
   end

   local function get_configuration(notifydata)
      local data = {}

      -- Set default data
      data["server"] = _server
      data["from"] = _from
      data["keydir"] = _keydir
      data["key"] = _key
      data["shellscript"] = _shellscript

      for i, item in pairs(notifydata)
      do
	 -- legal names: server, from, keydir, key, shellscript, comment
	 if item.name == "server" then
	    for j, val in pairs(item.values) do
	       data[item.name] = val
	    end
	 elseif item.name == "from" then
	    for j, val in pairs(item.values) do
	       data[item.name] = val
	    end
	 elseif item.name == "keydir" then
	    for j, val in pairs(item.values) do
	       data[item.name] = val
	    end
	 elseif item.name == "key" then
	    for j, val in pairs(item.values) do
	       data[item.name] = val
	    end
	 elseif item.name == "shellscript" then
	    for j, val in pairs(item.values) do
	       data[item.name] = val
	    end
	    -- Skip past other accepted words
	 elseif item.name == "pattern" then
	 elseif item.name == "allow" then
	 elseif item.name == "deny" then
	 elseif item.name == "continue" then
	 elseif item.name == "comment" then
	 else
	    io.stderr:write("unknown symbol in notify: " .. item.name .. "\n")
	 end
      end
      return data
   end

   local function get_notify_recipients(notifydata,branch)
      local results = {}
      local denied = {}
      local matches = false
      local cont = false

      for i, item in pairs(notifydata)
      do
	 -- Skip past other accepted words
	 if item.name == "server" then
	 elseif item.name == "from" then
	 elseif item.name == "keydir" then
	 elseif item.name == "key" then
	 elseif item.name == "shellscript" then
	    -- legal names: pattern, allow, deny, continue, comment
	 elseif item.name == "pattern" then
	    if matches and not cont then
	       return table_toarray(results)
	    end
	    matches = false
	    cont = false
	    for j, val in pairs(item.values) do
	       if globish_match(val, branch) then matches = true end
	    end
	 elseif item.name == "allow" then if matches then
	    for j, val in pairs(item.values) do
	       if nil == denied[val] then results[val] = val end
	    end
	 end elseif item.name == "deny" then if matches then
	    for j, val in pairs(item.values) do
	       denied[val] = val
	    end
	 end elseif item.name == "continue" then if matches then
	    cont = true
	    for j, val in pairs(item.values) do
	       if val == "false" or val == "no" then cont = false end
	    end
	 end elseif item.name ~= "comment" then
	    io.stderr:write("unknown symbol in notify: " .. item.name .. "\n")
	 end
      end
      return table_toarray(results)
   end

   local function summarize_certs(t)
      local str = "revision:            " .. t["revision"] .. "\n"
      local changelog
      for name,values in pairs(t["certs"]) do
	 local formatted_value = ""
	 for j,val in pairs(values) do
	    formatted_value = formatted_value .. name .. ":"
	    if string.match(val, "\n")
	    then formatted_value = formatted_value .. "\n"
	    else formatted_value = formatted_value .. (string.rep(" ", 20 - (# name))) end
	    formatted_value = formatted_value .. val .. "\n"
	 end
	 if name == "changelog" then changelog = formatted_value else str = str .. formatted_value end
      end
      if nil ~= changelog then str = str .. changelog end
      return (str .. "manifest:\n" .. t["manifest"])
   end

   local function make_subject_line(t)
      local str = ""
      for j,val in pairs(t["certs"]["branch"]) do
	 str = str .. val
	 if j < # t["certs"]["branch"] then str = str .. ", " end
      end
      return str .. ": " .. t["revision"]
   end

   local _emails_to_send = {}
   local _notify_data = {}
   local _configuration_data = {}

   push_hook_functions(
      {
	 start =
	    function (session_id, my_role, sync_type,
		      remote_host, remote_keyname, includes, excludes)
	       _notify_data = parse_configuration()
	       _configuration_data = get_configuration(_notify_data)
	       _emails_to_send[session_id] = {}
	       return "continue",nil
	    end,

	 revision_received =
	    function (new_id, revision, certs, session_id)
	       if _emails_to_send[session_id] == nil then
		  -- no session present
		  return "continue",nil
	       end

	       local rev_data = {["certs"] = {},
				 ["revision"] = new_id,
				 ["manifest"] = revision}
	       for _,cert in ipairs(certs) do
		  if cert["name"] == "branch" then
		     rev_data["recipients"] =
			get_notify_recipients(_notify_data, cert["value"])
		  end
		  if cert["name"] ~= nil then
		     if nil == rev_data["certs"][cert["name"]] then
			rev_data["certs"][cert["name"]] = {}
		     end
		     table.insert(rev_data["certs"][cert["name"]],
				  cert["value"])
		  end
	       end
	       _emails_to_send[session_id][new_id] = rev_data
	       return "continue",nil
	    end,

	 ["end"] =
	    function (session_id, status,
		      bytes_in, bytes_out,
		      certs_in, certs_out,
		      revs_in, revs_out,
		      keys_in, keys_out, ...)
	       if _emails_to_send[session_id] == nil then
		  -- no session present
		  return "continue", nil
	       end

	       if status ~= 200 then
		  -- some error occured, no further processing takes place
		  return "continue", nil
	       end

	       if _emails_to_send[session_id] == "" then
		  -- we got no interesting revisions
		  return "continue", nil
	       end

	       local from = _configuration_data["from"]
	       local server = _configuration_data["server"]
	       local keydir = _configuration_data["keydir"]
	       local key = _configuration_data["key"]
	       local shellscript = _configuration_data["shellscript"]

	       --print("monotone-mail-notify: About to write data")
	       for rev_id,rev_data in pairs(_emails_to_send[session_id]) do
		  if # (rev_data["recipients"]) > 0 then
		     local subject = make_subject_line(rev_data)
		     local reply_to = ""
		     for j,auth in pairs(rev_data["certs"]["author"]) do
			reply_to = reply_to .. auth
			if j < # (rev_data["certs"]["author"]) then
			   reply_to = reply_to .. ", "
			end
		     end

		     local now = os.time()

		     --print("monotone-mail-notify: Writing data for revision ",
		     --	rev_data["revision"])

		     local outputFileRev = io.open(_base .. now .. "." .. rev_data["revision"] .. ".rev.txt", "w+")
		     local outputFileHdr = io.open(_base .. now .. "." .. rev_data["revision"] .. ".hdr.txt", "w+")
		     local outputFileDat = io.open(_base .. now .. "." .. rev_data["revision"] .. ".dat.txt", "w+")

		     local to = ""
		     for j,addr in pairs(rev_data["recipients"]) do
			to = to .. addr
			if j < # (rev_data["recipients"]) then
			   to = to .. ", "
			end
		     end

		     outputFileDat:write("server='" .. server .. "'\n")
		     outputFileDat:write("keydir='" .. keydir .. "'\n")
		     outputFileDat:write("key='" .. key .. "'\n")
		     outputFileDat:close()

		     outputFileHdr:write("To: " .. to .. "\n")
		     outputFileHdr:write("From: " .. from .. "\n")
		     outputFileHdr:write("Subject: " .. subject .. "\n")
		     outputFileHdr:write("Reply-To: " .. reply_to .. "\n")
		     outputFileHdr:close()

		     outputFileRev:write(summarize_certs(rev_data))
		     outputFileRev:close()
		  end
	       end

	       if shellscript and shellscript ~= "" then
		  print("monotone-mail-notify.lua: Running script ",
			shellscript)
		  spawn_redirected("/dev/null",
				   _shellscript_log,
				   _shellscript_errlog,
				   "bash", shellscript)
	       end
	       _emails_to_send[session_id] = nil
	       return "continue",nil
	    end
      })
end

