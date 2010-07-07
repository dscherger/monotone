
check(get("rcfile.lua"))

mtn_setup()

function mt(...)
    return nodb_mtn("--rcfile", "rcfile.lua", ...)
end

-- at first some fun with invalid database aliases
check(mt("ls", "branches", "-d", ":not-existing.mtn"), 1, false, true)
check(qgrep("managed_databases/not-existing.mtn.+does.+not.+exist", "stderr"))

check(mt("ls", "branches", "-d", ":"), 1, false, true)
check(qgrep("invalid database alias ':': must not be empty", "stderr"))

check(mt("ls", "branches", "-d", ":/foo.mtn"), 1, false, true)
check(qgrep("invalid database alias ':/foo.mtn': does contain invalid characters", "stderr"))

-- check that this invalidness is also checked for values from the hook
check(mt("setup", "-b", "foo", "foo"), 1, false, true)
check(qgrep("invalid database alias 'invalid': does not start with a colon", "stderr"))

-- now check if we get prompted to resolve a ambiguous alias
mkdir("managed_databases")
mkdir("other_managed_databases")

check(nodb_mtn("db", "init", "-d", "managed_databases/foo.mtn"), 0, false, false)
check(nodb_mtn("db", "init", "-d", "other_managed_databases/foo.mtn"), 0, false, false)

check(mt("ls", "branches", "-d", ":foo.mtn"), 1, false, true)
check(qgrep("the database alias ':foo.mtn' has multiple ambiguous expansions", "stderr"))
check(qgrep("managed_databases/foo.mtn", "stderr"))
check(qgrep("other_managed_databases/foo.mtn", "stderr"))

