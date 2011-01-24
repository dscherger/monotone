-- Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
--
-- This program is made available under the GNU GPL version 2.0 or
-- greater. See the accompanying file COPYING for details.
--
-- This program is distributed WITHOUT ANY WARRANTY; without even the
-- implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
-- PURPOSE.

-- functions can be redefined
foo = "bar"
old_L = L
L = function () unlogged_mkdir("xxx") end
mkdir("bar") -- calls L()
L = old_L
check(exists("xxx"))

-- part 1: edit some globals for the next test...
foo = "bar"
L = nil
