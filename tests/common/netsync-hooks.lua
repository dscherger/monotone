
do
   local old = get_netsync_read_permitted
   function get_netsync_read_permitted(pattern, identity)
      local permfile = io.open(get_confdir() .. "/read-permissions", "r")
      if (permfile == nil) then
	 return true
      end
      io.close(permfile)
      return old(pattern, identity)
   end
end

do
   local old = get_netsync_write_permitted
   function get_netsync_write_permitted(identity)
      local permfile = io.open(get_confdir() .. "/write-permissions", "r")
      if (permfile == nil) then
	 return true
      end
      io.close(permfile)
      return old(identity)
   end
end

do
   local old = get_remote_automate_permitted
   function get_remote_automate_permitted(ident, args, opts)
      if ident.given_name == "tester@test.net" then return true end
      return old(ident, args, opts)
   end
end
