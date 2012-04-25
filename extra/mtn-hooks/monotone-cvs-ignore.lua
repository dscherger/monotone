do

   local _glob_to_pattern = function (glob)
      local pattern
	
      -- escape all special characters:
      pattern = string.gsub(glob, "([%^%$%(%)%%%.%[%]%*%+%-%?])", "%%%1")

      -- convert the glob's ones to pattern's:
      pattern = string.gsub(pattern, "%%%*", "[^/]*")
      pattern = string.gsub(pattern, "%%%?", ".")

      return pattern
   end

   local old_ignore_file = ignore_file
   function ignore_file(name)
      local dir, pat1, pat2

      dir = string.gsub(name, "/[^/]+$", "/")
      if (dir == name) then dir = "" end
      pat1 = "^" .. _glob_to_pattern(dir)

      local handle, msg = io.open(dir .. ".cvsignore")
      if (handle) then
	 for line in handle:lines() do
	    pat2 = _glob_to_pattern(line) .. "$"
	    if (string.find(name, pat1 .. pat2)) then
	       return true
	    end
	 end
	 io.close(handle)
      end

      return old_ignore_file(name)
   end
end
