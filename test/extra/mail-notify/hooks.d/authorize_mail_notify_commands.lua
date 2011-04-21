do
   local _save_get_remote_automate_permitted = get_remote_automate_permitted
   function get_remote_automate_permitted(key_identity, command, options)
      local mail_notify_commands = {
	 "parents", "get_revision", "get_file", "content_diff"
      }
      for _,v in ipairs(mail_notify_commands) do
	 if (v == command[1]) then
	    return true
	 end
      end

      return false
   end
end
