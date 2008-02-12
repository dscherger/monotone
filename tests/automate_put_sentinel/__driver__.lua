mtn_setup()

sentinel_id = "4c2c1d846fa561601254200918fba1fd71e6795d"
sentinel = "format_version \"1\"\n\nnew_manifest [8c7ed0236ac7b7a36ae7b09c21d2c308303f95a8]\n\nold_revision []\n\nadd_dir \"\"\n\nadd_file \"foo\"\n content [00000000000000000000000000000000deadbeef]\n"

check(mtn("--debug", "automate", "put_sentinel", sentinel_id, sentinel), 0, true, false)

check(mtn("automate", "get_revision", sentinel_id), 1, false, true)
canonicalize("stderr")
e = readfile("stderr")
check(e == "mtn: error: missing revision "..sentinel_id.."\n")

check(mtn("automate", "get_sentinel", sentinel_id), 0, true, false)
canonicalize("stdout")
o = readfile("stdout")
check(o == sentinel)

check(mtn("automate", "get_manifest_of", sentinel_id), 0, true, false)

-- add a file for the revision we put on top of the sentinel
check(mtn("automate", "put_file", "contents of foo"), 0, true, false)
canonicalize("stdout")
file_hash = "12b7fe4fb8d865f2215d85c36dd6fc250987b9ec"
result = readfile("stdout")
check(result == file_hash.."\n")

rev = "format_version \"1\"\n\nnew_manifest [00000000000000000000000000000000deadbeef]\n\nold_revision ["..sentinel_id.."]\n\npatch \"foo\"\n from [00000000000000000000000000000000deadbeef]\n   to ["..file_hash.."]\n"

check(mtn("automate", "put_revision", rev), 0, true, false)
canonicalize("stdout")
rev = "de0289cb69fecad31d732579b6c81e27282fef6a"
result = readfile("stdout")
check(result == rev.."\n")

check(mtn("automate", "cert", rev, "author", "tester@test.net"), 0, true, false)
check(mtn("automate", "cert", rev, "branch", "testbranch"), 0, true, false)
check(mtn("automate", "cert", rev, "changelog", "blah-blah"), 0, true, false)
check(mtn("automate", "cert", rev, "date", "2007-05-28T13:33:33"), 0, true, false)

check(mtn("co", "-b", "testbranch", "mtnco"), 0, false, false)

-- check if log informs about missing revisions
check(indir("mtnco", mtn("log", "--no-graph")), 0, true, false)

-- try to get a diff from log
check(indir("mtnco", mtn("--debug", "log", "--diffs", "--no-graph")), 0, true, true)

-- try a direct diff 
check(indir("mtnco", mtn("diff", "-r", sentinel_id)), 1, false, true)
canonicalize("stderr")
e = readfile("stderr")
check(e == "mtn: error: missing revision '"..sentinel_id.."'\n")

-- try annotate
-- check(indir("mtnco", mtn("annotate", "foo")), 0, false, false)

