--  Copyright (C) Nathaniel Smith <njs@pobox.com>
--                Timothy Brownawell <tbrownaw@gmail.com>
--                Thomas Moschny <thomas.moschny@gmx.de>
--                Richard Levitte <richard@levitte.org>
--  Licensed under the MIT license:
--    http://www.opensource.org/licenses/mit-license.html
--  I.e., do what you like, but keep copyright and there's NO WARRANTY.
-- 
--  CIA bot client for monotone, Lua part.  This works in conjuction with
--  ciabot_monotone_hookversion.py.

do
   -- Configure with the path to the corresponding python script
   if not ciabot_python_script then
      error "Please configure by defining 'ciabot_python_script' with the path to the python script ciabot_monotone_hookversion.py"
   end

   push_hook_functions({
			    revision_received =
			       function (rid, rdat, certs)
				  local branch, author, changelog
				  for i, cert in pairs(certs)
				  do
				     if (cert.name == "branch") then
					branch = cert.value
				     end
				     if (cert.name == "author") then
					author = cert.value
				     end
				     if (cert.name == "changelog") then
					changelog = cert.value
				     end
				  end
				  wait(spawn(ciabot_python_script,
					     get_confdir(), rid,
					     branch, author, changelog,
					     rdat))
				  return "continue",nil
			       end
			 })
end
