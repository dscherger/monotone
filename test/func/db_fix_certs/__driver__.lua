mtn_setup()

get("test.mtn")
get("other_key")
getstd("test_keys")
check(mtn("-d", "test.mtn", "read", "test_keys"), 0, nil, false)

check(mtn("-d", "test.mtn", "db", "execute",
          "select 'There are '||count(*)||' certs' from revision_certs"), 0, true)
check(qgrep("There are 8 certs", "stdout"))

check(mtn("-d", "test.mtn", "db", "migrate"), 0, nil, false)

check(mtn("-d", "test.mtn", "db", "execute",
          "select 'There are '||count(*)||' certs' from revision_certs"), 0, true)
check(qgrep("There are 8 certs", "stdout"))

check(mtn("-d", "test.mtn", "read", "other_key"), 0, nil, false)

-- should fix 1 (the one by the 'other' key we loaded)
check(mtn("-d", "test.mtn", "db", "fix_certs"), 0, nil, true)
check(qgrep("checked 8 certs, found 3 bad, fixed 1$", "stderr"))

-- should drop 2 (one by a normal key we didn't load, and
-- one by a renamed copy of the standard key (when checking
-- against the standard key, that one will OK the sig, but
-- give a different cert hash))
check(mtn("-d", "test.mtn", "db", "fix_certs", "--drop-bad-certs"), 0, nil, true)
check(qgrep("checked 8 certs, found 2 bad, fixed 0, dropped 2$", "stderr"))
