-- This is a very quick test to see that db migrate doesn't do more
-- than it should.  The reason for this test is that mtn version 0.99
-- would regenerate certain caches even when they were freshly regenerated.
--
-- The easiest way to check this is on a newly created database, which
-- mtn_setup() kindly provides.

mtn_setup()

check(get("expected-stderr.txt"))
check(mtn("db", "migrate"), 0, false, true)
check(samefile("stderr", "expected-stderr.txt"))
