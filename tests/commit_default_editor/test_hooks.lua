
function execute(path,...) 
   tname, rest = unpack(arg)
   if tname == nil then
      return 1
   end
   if path == "editor" then
      tmp = io.open(tname, "w")
      tmp:write("Hello\n")
      io.close(tmp)
      return 0
   end
   return 1
end

function get_passphrase(keyid)
   return keyid
end

function program_exists_in_path(program)
   return (program == "editor")
end

