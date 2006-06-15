
-- the included database is valid except for all paths being "./" prefixed,
-- hence the database doesn't have correctly normalised revisions.

-- for future reference, it was created with a modified monotone binary,
-- paths.cc was modified. Also, the 'add_dir "."' was added to _MTN/work by hand.

--old_revision 0
--
--add_dir ""
--
--add_dir "."
--
--add_file "./file"

mtn_setup()

getfile("bad.db")

check(mtn("db", "migrate", "-d", "bad.db"), 0, false, false)

check(mtn("db", "check", "-d", "bad.db"), 1, false, true)

check(qgrep('revisions not parseable', "stderr"))
