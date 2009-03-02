--
--  init.lua -- Monotone Lua extension command "mtn init"
--  Copyright (C) 2008 Ralf S. Engelschall <rse@engelschall.com>
--
--  This program is made available under the GNU GPL version 2.0 or
--  greater. See the accompanying file COPYING for details.
--
--  This program is distributed WITHOUT ANY WARRANTY; without even the
--  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
--  PURPOSE.
--

register_command(
    "init", "BRANCH",
    "Place current directory under local version control.",
    "Creates a new _MTN/mtn.db database and places the local " ..
    "directory tree under version control using this database.",
    "command_init"
)   

function command_init(branch)
    local db_fn = "mtn.db"

    --  sanity check command line
    if branch == nil then
        io.stderr:write("mtn: init: ERROR: no branch specified\n")
        return
    end

    --  create new database 
    execute("mtn", "--db=".. db_fn , "db", "init")

    --  place current directory under version control
    execute("mtn", "--db=" .. db_fn, "setup", "-b", branch)

    --  place database into book-keeping directory
    execute("mv", db_fn, "_MTN/")

    --  perform a simple operation so that Monotone
    --  updates the book-keeping directory and the options file
    execute("sh", "-c", "mtn --db=_MTN/".. db_fn .. " stat >/dev/null 2>&1")
end

