
function execute(path,...) 
   tname, rest = ...
   if tname == nil then
      return 1
   end
   if path == "editor" then

      tmp = io.open(tname, "r")
      text = tmp:read("*a")
      io.close(tmp)

      text = string.gsub(text, "\nChangeLog: \n\n\n", "\nChangeLog: \n\nHello\n")

      tmp = io.open(tname, "w")
      tmp:write(text)
      io.close(tmp)

      return 0
   end
   return 1
end

function get_passphrase(keyid)
   return keyid.given_name
end

function program_exists_in_path(program)
   return (program == "editor")
end

