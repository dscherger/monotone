-- Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
--
-- This program is made available under the GNU GPL version 2.0 or
-- greater. See the accompanying file COPYING for details.
--
-- This program is distributed WITHOUT ANY WARRANTY; without even the
-- implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
-- PURPOSE.

function ignore_file(name)
if (string.find(name, "%~$")) then return true end
return false
end
