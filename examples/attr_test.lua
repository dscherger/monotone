
-- WARNING, WARNING, WARNING; Use of execout in this script is solely for the
-- purposes of testing prior to the coding of execute_redirout (later in this
-- branch)
-- The mtn-dosh script could be subverted by the use of shell meta-characters
-- in the tempfile filename.
-- You have been warned.

function getdbgval()
    return 0
end

 function tmpname()
   local tdir
   tdir = os.getenv("TMPDIR")
   if tdir == nil then tdir = os.getenv("TMP") end
  if tdir == nil then tdir = os.getenv("TEMP") end
   if tdir == nil then tdir = "/tmp" end
  local tmpname = string.format("%s/mtn.tmp.", tdir)
   local getrand
   local geterrstr 
   local randhnd, errstr = io.open('/dev/urandom')
   if (randhnd) then
       getrand, geterrstr = randhnd:read(8)
	if (getrand) then
         math.randomseed(string.byte(getrand, 1, 8))
       else
          print(geterrstr)
           math.randomseed(os.time())
       end 
       randhnd:close()
       for digitnum = 1,10 do
           tmpname = tmpname .. string.char(math.random(string.byte('a'),string.byte('z')))
       end
   else
       print(errstr)
       tmpname = tmpname .. 'XXXXXXXXXX'
   end
   if (getdbgval() > 2) then print("Temp file is: " .. tmpname) end
   return tmpname
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
       
function execout(command, ...)
   local out = nil
   local exec_retval
   local tmpfile = tmpname()
   local outhnd
   local errstr
   local out
   local tmpfile = temp_file('mtn')
   exec_retval = execute('mtn-dosh', tmpfile, command, unpack(arg)) 
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
            print('execout; got ' .. out)
        else
            print('no output')
        end
    end
    return out
end

-- function get_defperms(filetype, is_executable)
--     local defperms
--     local perms = execout('sh', '-c', 'umask')
--     if (perms) then
--         if ((filetype == "regular file") or (filetype == "regular empty file")) then
--             if (is_executable) then
--                 defperms = 777 - tonumber(string.sub(perms, 2))
--             else
--                 defperms = 666 - tonumber(string.sub(perms, 2))
--             end
--         elseif (filetype == "directory") then
--            defperms = 777 - tonumber(string.sub(perms, 2))
--         else
--             if (getdbgval() > 2) then
--                 print("Not a regular file or directory, is: " .. filetype)
--             end
--             defperms = nil
--         end
--     else
--         defperms = nil
--     end
--     if (defperms) then
--         defpermsstr = string.format("%03d", defperms)
--     end
--     if ((getdbgval() > 2) and defpermsstr) then
--         print("defperms = " .. defpermsstr)
--     end
--     return defpermsstr
-- end

-- function get_defuser()
--     defuser = execout('id', '-u')
--     if ((getdbgval() > 2) and defuser) then
--         print("defuser = " .. defuser)
--     end
--     return defuser
-- end

-- function get_defgroup()
--     defgroup = execout('id', '-g')
--     if ((getdbgval() > 2) and defgroup) then
--         print("defgroup = " .. defgroup)
--     end 
--     return defgroup
-- end

function has_perms(filename)
--  local defperms
    local perms = execout('stat', '-c', '%a', filename)
--  local permnum
    local retperm = nil
--  local filetype

    if (perms) then
--        filetype = execout('stat', '-c', '%F', filename)
--      if (filetype) then
--            defperms = get_defperms(filetype, is_executable(filename))
--	    if (defperms ~= nil) then 
	        permnum = tonumber(perms, 8)
--              if (permnum ~= tonumber(defperms)) then
                  retperm = string.format('%04o', permnum) 
--                    retperm = perms
--              end
--          end
--      end  
    end
    if ((getdbgval() > 2) and retperm) then
        print("perms = " .. retperm)
    end
    return retperm
end

function has_user(filename)
    local user = execout('stat', '-c', '%u', filename) 
--  local defuser = get_defuser()
    local retuser = nil
    if (user) then
--      if (defuser) then
--          if (user ~= defuser) then
                retuser = user
--            end
--        end
   end
   if ((getdbgval() > 2) and retuser) then
       print("user = " .. retuser)
   end
   return retuser
end
   
function is_symlink(filename)
    local filetype = execout('stat', '-c', '%F', filename)
    local link_target 
    local retlink = nil
    if (filetype == "symbolic link") then
	link_target = execout('readlink', filename)
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
    local group = execout('stat', '-c', '%g', filename) 
--    local defgroup
    local retgroup = nil
    if (group) then
--        if (defgroup) then
--            if (user ~= defgroup) then
                retgroup = group
--            end
--        end
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
    end
end

attr_functions["user"] = function(filename, value)
    if (value ~= nil) then
        execute("/bin/chown", value, filename)
    end
end

attr_functions["group"] = function(filename, value)
    if (value ~= nil) then
        execute("/bin/chgrp", value, filename)
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

