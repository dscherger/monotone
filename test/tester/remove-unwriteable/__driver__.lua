-- Copyright (C) 2006 Timothy Brownawell <tbrownaw@gmail.com>
--
-- This program is made available under the GNU GPL version 2.0 or
-- greater. See the accompanying file COPYING for details.
--
-- This program is distributed WITHOUT ANY WARRANTY; without even the
-- implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
-- PURPOSE.

skip_if(not existsonpath("chmod"))

mkdir("foo")
writefile("foo/bar", "quux")

check({"chmod", "a-w", "foo"})
check({"chmod", "a-w", "foo/bar"})
remove("foo")
check(not exists("foo"))
