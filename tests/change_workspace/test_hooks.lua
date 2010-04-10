function get_branch(workspace)
   local status, msg
   status, msg = change_workspace(workspace)
   if status then
      _, msg = mtn_automate("get_option", "branch")
      io.write(msg)
   else
      io.write(msg)
   end
end

register_command("get_branch", "workspace", "", "", "get_branch")

-- end of file
