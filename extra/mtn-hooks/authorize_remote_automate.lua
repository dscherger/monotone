-- Copyright (c) 2010, Thomas Keller <me@thomaskeller.biz>
--                     Richard Levitte <richard@levitte.org>
-- License: GPLv2 or later
--
-- This script reads key identities from a file "remote-automate-permissions"
-- in the configuration directory and permits those authenticating with one
-- of those keys to perform dangerous (read/write) remote automate operations.
-- The format of the file is very simple, one key identity on every line.
-- Lines starting with # are ignore, as well as empty lines.
--
-- It's possible to configure this script to allow the performance of some
-- remote automate commands anonymously, through the variable
-- ARA_safe_commands, which has to be a table of commands as strings.
-- One example configuration, taken from the setup at code.monotone.ca, could
-- be this:
--
-- ARA_safe_commands = {
--    "get_corresponding_path", "get_content_changed", "tags", "branches",
--    "common_ancestors", "packet_for_fdelta", "packet_for_fdata",
--    "packets_for_certs", "packet_for_rdata", "get_manifest_of",
--    "get_revision", "select", "graph", "children", "parents", "roots",
--    "leaves", "ancestry_difference", "toposort", "erase_ancestors",
--    "descendents", "ancestors", "heads", "get_file_of", "get_file",
--    "interface_version", "get_attributes", "content_diff",
--    "file_merge", "show_conflicts", "certs", "keys", "get_extended_manifest_of"
-- }

do
   local _safe_commands = {}
   if ARA_safe_commands then
      _safe_commands = ARA_safe_commands
   end

   function _get_remote_automate_permitted(key_identity, permfilename)
      if not exists(permfilename) or isdir(permfilename) then return false end
      local permfile = io.open(permfilename, "r")
      if (permfile == nil) then
	 return false
      end
      local matches = false
      local line = permfile:read()
      while (not matches and line ~= nil) do
	 if not globish_match("#*", line) then
	    local _, _, ln = string.find(line, "%s*([^%s]*)%s*")
	    if ln == "*" then matches = true end
	    if ln == key_identity.id then matches = true end
	    if globish_match(ln, key_identity.name) then matches = true end
	    line = permfile:read()
	 end
      end
      io.close(permfile)
      return matches
   end

   local _save_get_remote_automate_permitted = get_remote_automate_permitted
   function get_remote_automate_permitted(key_identity, command, options)
      local permfilename = get_confdir() .. "/remote-automate-permissions"
      local permdirname = permfilename .. ".d"

      -- See if the incoming key matches any of the key identities or
      -- patterns found in the permissions file.
      if _get_remote_automate_permitted(key_identity, permfilename) then
	 return true
      end
      if isdir(permdirname) then
	 local files = read_directory(permdirname)
	 table.sort(files)
	 for _,f in ipairs(files) do
	    pf = permdirname.."/"..f
	    if _get_remote_automate_permitted(key_identity, pf) then
	       return true
	    end
	 end
      end

      -- No matching key found, let's see if the command matches one the
      -- admin allowed to be performed anonymously
      for _,v in ipairs(_safe_commands) do
	 if (v == command[1]) then
	    return true
	 end
      end

      -- No matches found anywhere, then don't permit this operation
      return false
   end
end
