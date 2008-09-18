
mtn_setup()

-- loosely based on importing_cvs_files, but with some tags and an
-- ignore_cvs_symbol hook.

-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))
check(get("ignhook.lua"))

-- import into monotone and check presence of tags
check(mtn("--branch=testbranch", "--debug", "--rcfile=ignhook.lua", "cvs_import", "cvs-repository/test"), 0, false, false)
check(mtn("ls", "tags"), 0, true, false)

-- check if we have the tag we want..
check(qgrep("TAG_TO_RESPECT", "stdout"))

-- ..but not the one we don't
check(not qgrep("TAG_TO_IGNORE", "stdout"))

