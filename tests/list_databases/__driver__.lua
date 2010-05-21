check(getstd("test_keys"))
check(getstd("test_hooks.lua"))
check(mtn("read", "test_keys"), 0, false, false)
remove("test_keys")

check(get("rcfile.lua"))
mkdir("managed_databases")

function mt(...)
    return mtn("--rcfile", "rcfile.lua", ...)
end

check(mt("ls", "dbs"), 0, true, false)
check(samelines("stdout", {}))

writefile("managed_databases/foo")
check(mt("ls", "dbs"), 0, true, false)
check(samelines("stdout", {}))

writefile("managed_databases/foo.mtn")
check(mt("ls", "dbs"), 0, true, false)
check(samelines("stdout", {}))

check(mt("db", "init", "-d", ":bar"), 0, false, false)
check(exists("managed_databases/bar.mtn"))

check(mt("ls", "dbs"), 0, true, false)
check(qgrep(":bar.mtn.+in.+list_databases\/managed_databases", "stdout"))
check(qgrep("\tno known valid workspaces", "stdout"))

check(mt("setup", "-d", ":bar", "-b", "test.foo.branch", "test_foo"), 0, false, false)

check(mt("ls", "dbs"), 0, true, false)
check(not qgrep("\tno known valid workspaces", "stdout"))
check(qgrep("\ttest.foo.branch.+in.+list_databases\/test_foo", "stdout"))

