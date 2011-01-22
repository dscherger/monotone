-- Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
--
-- This program is made available under the GNU GPL version 2.0 or
-- greater. See the accompanying file COPYING for details.
--
-- This program is distributed WITHOUT ANY WARRANTY; without even the
-- implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
-- PURPOSE.

-- the variables set by cleanup-1 should not have survived to this point
check(t_ran == nil)
check(cleanup_ran == nil)
check(test.t_ran == nil)
check(test.cleanup_ran == nil)
