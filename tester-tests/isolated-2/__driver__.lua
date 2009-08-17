-- Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
--
-- This program is made available under the GNU GPL version 2.0 or
-- greater. See the accompanying file COPYING for details.
--
-- This program is distributed WITHOUT ANY WARRANTY; without even the
-- implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
-- PURPOSE.

-- part 2: ...check that those globals have been reset
check(foo == nil)
mkdir("abc")
check(not exists("xxx"))
L("...") -- "attempt to call nil value"
