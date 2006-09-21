function getdbgval()
    return 0
end

function argstr(...)
   local argstr = ""
   for i,v in ipairs(arg) do
       if (argstr ~= "") then
           argstr = argstr .. ' '
       end
       argstr = argstr .. tostring(v) 
   end
   if (getdbgval() > 3) then print("Argument string is: " .. argstr) end
   return argstr
end
       
function execute_out(command, ...)
   local out = nil
   local exec_retval
   local outhnd
   local errstr
   local tmpfile = temp_file()
   tmpfile:close()
   exec_retval = execute_redirout(command, tmpfile, unpack(arg)) 
   if (exec_retval == 0) then
      outhnd, errstr = io.open(tmpfile)
      if (outhnd) then
          out, errstr = outhnd:read() 
          if (out == nil) then
              if (getdbgval() > 1) then
                  print("Error reading " .. tmpfile .. ": " .. errstr)
              end
          end
          outhnd:close()
          os.remove(tmpfile)
      else
          print("Error opening " .. tmpfile .. ": " .. errstr)
          os.remove(tmpfile)
      end 
   else
       print('Error executing ' .. argstr(command, unpack(arg)) .. ' > ' .. tmpfile .. ': ' .. tostring(exec_retval))
 os.remove(tmpfile)
   end

   if (getdbgval() > 1) then
        if (out ~= nil) then
            print('execute_out; got ' .. out)
        else
            print('no output')
        end
   end
   return out
end

function get_defperms(filetype, is_executable)
   local defperms
   local perms = execute_out('sh', '-c', 'umask')
   if (perms) then
       -- This magic tries to be intelligent about setuid, setguid, and sticky
       -- bits.  The default creation mask for these bits is 0, and the default
       -- umask is 0 (so creating or copying a file has these bits be zeroed);
       -- the other permission bits are assumed to be on unless turned off by
       -- by the umask.  Therefore we handle the set bits specially.
       -- We (only in this script) invert the set bits for our internal mask,
       -- so that we end up returning the same permissions as the shell would
       -- use in creating a new file), unless the mask is 0, in which case we
       -- leave it alone (all set bits off).  This as the consequence that we
       -- can never end up with an internal mask of 7 (all set bits on).
       local setbits = tonumber(string.sub(perms, 1, 1), 8)
       if (setbits ~= 0) then
           setbits =  not setbits
       end
       
       if (filetype ~= "directory") then
           -- permissions are specified in octal (base 8)
           if (is_executable) then		
               defperms = tonumber(string.format("777"), 8) - tonumber(string.sub(perms, 2), 8)
           else
               defperms = tonumber(string.format("666"), 8) - tonumber(string.sub(perms, 2), 8)
           end
       else
          defperms = tonumber(string.format("777"), 8) - tonumber(string.sub(perms, 2), 8)
       end
    else
        defperms = nil
    end
    if (defperms) then
        defpermsstr = string.format("%04o", defperms)
    end
    if ((getdbgval() > 2) and defpermsstr) then
        print("defperms = " .. defpermsstr)
    end
    return defpermsstr
end

function get_defuser()
     defuser = execute_out('id', '-un')
     if ((getdbgval() > 2) and defuser) then
         print("defuser = " .. defuser)
     end
     return defuser
end

function get_defgroup()
    defgroup = execute_out('id', '-gn')
    if ((getdbgval() > 2) and defgroup) then
        print("defgroup = " .. defgroup)
    end 
    return defgroup
end

function has_perms(filename)
    local perms = execute_out('stat', '-c', '%a', filename)
    local retperm = nil

    if (perms) then
	permnum = tonumber(perms, 8)
	retperm = string.format('%04o', permnum) 
    end
    if ((getdbgval() > 2) and retperm) then
        print("perms = " .. retperm)
    end
    return retperm
end

function has_user(filename)
   local user = execute_out('stat', '-c', '%U', filename) 
   local retuser = nil
   if (user) then
       retuser = user
   end
   if ((getdbgval() > 2) and retuser) then
       print("user = " .. retuser)
   end
   return retuser
end
   
function is_symlink(filename)
    local filetype = execute_out('stat', '-c', '%F', filename)
    local link_target 
    local retlink = nil
    if (filetype == "symbolic link") then
	link_target = execute_out('readlink', filename)
        if (link_target) then
            if (link_target ~= "" ) then
                retlink = link_target
            end
        end
    end
    if ((getdbgval() > 2) and retlink) then
       print("linktarget = " .. retlink)
    end
    return retlink
end
   
function has_group(filename)
    local group = execute_out('stat', '-c', '%G', filename) 
    local retgroup = nil
    if (group) then
       retgroup = group
    end
    if ((getdbgval() > 2) and retgroup) then
       print("group = " .. retgroup)
    end
    return retgroup
end

attr_init_functions["perms"] = function(filename)
    return has_perms(filename)
end

attr_init_functions["user"] = function(filename)
    return has_user(filename)
end

attr_init_functions["group"] = function(filename)
    return has_group(filename)
end

attr_init_functions["symlink"] = function(filename)
    return is_symlink(filename)
end

attr_functions["perms"] = function(filename, value)
    if (value ~= nil) then
        execute("/bin/chmod", value, filename)
    else
        execute("/bin/chmod", get_defperms(), filename)
    end
end

attr_functions["user"] = function(filename, value)
    if (value ~= nil) then
        execute("/bin/chown", value, filename)
    else
        execute("/bin/chown", get_defuser(), filename)
    end
end

attr_functions["group"] = function(filename, value)
    if (value ~= nil) then
        execute("/bin/chgrp", value, filename)
    else
        execute("/bin/chgrp", get_defgroup(), filename)
    end
end

attr_functions["symlink"] = function(filename, value)
    if (value ~= nil) then
        execute("/bin/mv", filename, filename .. ".old")
        if (execute("/bin/ln", "-s", value, filename) == 0) then
            os.remove(filename .. ".old")
        else
            execute("/bin/cp", '-a', filename .. ".old", filename)
        end
    else
        execute("/bin/mv", filename, filename .. ".old")
        local realname = execute("/bin/readlink", filename)
        if (realname ~= nil) then
            if (execute("/bin/cp", '-a', realname, filename) == 0) then
                os.remove(filename .. ".old")
            else
                execute("/bin/cp", '-a', filename .. ".old", filename)
            end
        end                        
    end
end

