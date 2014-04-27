-- Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
--
-- This program is made available under the GNU GPL version 2.0 or
-- greater. See the accompanying file COPYING for details.
--
-- This program is distributed WITHOUT ANY WARRANTY; without even the
-- implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
-- PURPOSE.

-- no state whatsoever is inherited across tests
-- (see cleanup-2 for the other half of this test)

function cleanup()
   cleanup_ran = true
   test.cleanup_ran = true
end

t_ran = true
test.t_ran = true
