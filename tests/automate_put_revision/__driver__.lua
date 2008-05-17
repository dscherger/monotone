mtn_setup()

rev = "format_version \"2\"\n\nnew_manifest [0000000000000000000000000000000000000004]\n\nold_revision []\n\nadd_dir \"\"\n\nadd_file \"foo\"\n content [5bf1fd927dfb8679496a2e6cf00cbe50c1c87145]\n"

check(mtn("automate", "put_file", "blah"), 0, true, false)
canonicalize("stdout")
file = "5bf1fd927dfb8679496a2e6cf00cbe50c1c87145"
result = readfile("stdout")
check(result == file.."\n")

check(mtn("automate", "put_revision", rev), 0, true, false)
canonicalize("stdout")
old_rev = "e12272e25a690ee6c23b2e1b80d7f67866aacaef"
result = readfile("stdout")
check(result == old_rev.."\n")

check(mtn("automate", "cert", old_rev, "author", "tester@test.net"), 0, true, false)
check(mtn("automate", "cert", old_rev, "branch", "testbranch"), 0, true, false)
check(mtn("automate", "cert", old_rev, "changelog", "blah-blah"), 0, true, false)
check(mtn("automate", "cert", old_rev, "date", "2005-05-21T12:30:51"), 0, true, false)

check(mtn("automate", "heads", "testbranch"), 0, true, false)
canonicalize("stdout")
check(old_rev.."\n" == readfile("stdout"))

--
-- this should trigger an invariant
-- I'm trying to re-add a file which already exists
--
rev = "format_version \"2\"\n\nnew_manifest [0000000000000000000000000000000000000000]\n\nold_revision [" .. old_rev .. "]\n\nadd_file \"foo\"\n content [" .. file .. "]\n"
check(mtn("automate", "put_revision", rev), 3, false, false)

-- but this should work (tests that we can use put_revision to commit a
-- single-parent revision)
check(mtn("automate", "put_file", ""), 0, false, false)
rev = "format_version \"2\"\n\nnew_manifest [0000000000000000000000000000000000000000]\n\nold_revision [" .. old_rev .. "]\n\patch \"foo\"\n from [" .. file .. "] to [da39a3ee5e6b4b0d3255bfef95601890afd80709]\n"
check(mtn("automate", "put_revision", rev), 0, false, false)
