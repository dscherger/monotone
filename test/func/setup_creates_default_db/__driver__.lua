
check(getstd("test_keys"))
check(getstd("test_hooks.lua"))
check(getstd("min_hooks.lua"))
check(get("rcfile.lua"))

-- create a new database initially
check(nodb_mtn("setup", "--rcfile", "rcfile.lua", "--branch=test1", "test1"), 0, false, true)
check(qgrep("initializing new database", "stderr"))
check(indir("test1", mtn("au", "get_option", "database")), 0, true, false)
rename("stdout", "test1_db")

check(exists("managed_databases/default.mtn"))
check(fsize("managed_databases/default.mtn") > 0)

-- but re-use it if it already exists
check(nodb_mtn("setup", "--rcfile", "rcfile.lua", "--branch=test2", "test2"), 0, false, true)
check(not qgrep("initializing new database", "stderr"))
check(indir("test2", mtn("au", "get_option", "database")), 0, true, false)
rename("stdout", "test2_db")

-- if everything goes well, both workspaces should have the same 
-- database and both should be registered
check(samefile("test1_db", "test2_db"))
check(nodb_mtn("ls", "dbs", "--rcfile", "rcfile.lua"), 0, true, false)
check(qgrep("test1", "stdout"))
check(qgrep("test2", "stdout"))
