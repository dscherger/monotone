
mtn_setup()

-- A few times in our history we've had to do data migration beyond
-- what the "db migrate" command can handle -- i.e., "db changesetify"
-- and "db rosterify".  We would like for it to be the case, that all
-- other commands give a nice error message if these commands need to
-- be run.  This test makes sure that monotone exits with an error if
-- given a db that appears to be very old.

-- The database dumps used here are "pre-migrated". They were run
-- through "db migrate" but still need "rosterify" or "changesetify"
-- to be run. But because they've already been migrated to a more
-- recent schema, "db migrate" doesn't know this. So it tries to
-- run regenerate_caches, which will E() about this just like any
-- other command should.
--
-- FIXME:
-- I suppose this is a good thing to test on its own, but there should
-- really also be a schema_migration_with_changesetify using "real"
-- pre-changeset db dumps, to go with schema_migration_with_rosterify.

check(get("changesetify.db.dumped", "stdin"))
check(mtn("-d", "cs-modern.db", "db", "load"), 0, false, false, true)
check(mtn("-d", "cs-modern.db", "db", "migrate"), 1, false, true)
check(qgrep("needs to be upgraded", "stderr"))

check(mtn("-d", "cs-modern.db", "ls", "keys"), 1, false, false)
check(mtn("-d", "cs-modern.db", "ls", "branches"), 1, false, false)


check(get("rosterify.db.dumped", "stdin"))
check(mtn("-d", "ro-modern.db", "db", "load"), 0, false, false, true)
check(mtn("-d", "ro-modern.db", "db", "migrate"), 1, false, true)
check(qgrep("project leader", "stderr"))

check(mtn("-d", "ro-modern.db", "ls", "keys"), 1, false, false)
check(mtn("-d", "ro-modern.db", "ls", "branches"), 1, false, false)

-- arguably "db regenerate_caches" should go here too -- it's treated
-- similarly.  But the test "schema_migration" tests for its behavior in this
-- case.
