-- this test checks whether regenerate caches honors sentinels and 
-- horizon_manifests

mtn_setup()

-- We don't want the standard db, we want full control ourselves
remove("test.db")

get("sample.db")
check(mtn("--db=sample.db", "db", "load"), 0, false, false, true)

check(mtn("db", "regenerate_caches"), 0, false, false, true)

check(mtn("db", "check"), 0, false, false, true)
