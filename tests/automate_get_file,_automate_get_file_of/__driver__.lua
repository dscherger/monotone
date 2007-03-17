
mtn_setup()

fileid = "4cbd040533a2f43fc6691d773d510cda70f4126a"

writefile("expected", "blah\n")
addfile("foo", "blah\n")

check(mtn("commit", "--date=2005-05-21T12:30:51",
          "--branch=testbranch", "--message=blah-blah"), 0, false, false)
rev1 = base_revision()

--
-- get_file tests
--

-- check that a correct usage produces correctly formatted output
check(mtn("automate", "get-file", fileid), 0, true, false)
canonicalize("stdout")
check(samefile("expected", "stdout"))

-- ensure that missing revisions fail
check(mtn("automate", "get-file", string.rep("0", 40)), 1, true, false)
check(fsize("stdout") == 0)

-- ensure that revisions are not being completed
check(mtn("automate", "get-file", string.sub(fileid, 1, 30)), 1, true, false)
check(fsize("stdout") == 0)

--
-- get_file_of tests
--

-- check if the file is properly outputted
check(mtn("automate", "get-file-of", "foo"), 0, true, false)
canonicalize("stdout")
check(samefile("expected", "stdout"))

-- ensure that unknown paths fail
check(mtn("automate", "get-file-of", "bar"), 1, true, false)
check(fsize("stdout") == 0)

-- ensure that unknown revisions fail
check(mtn("automate", "get-file-of", "-r", string.rep("0", 40), filename), 1, true, false)
check(fsize("stdout") == 0)

-- ensure that a former revision's file contents are readable as well
writefile("foo", "foobar\n");
commit()
rev2 = base_revision()
check(mtn("automate", "get-file-of", "-r", rev1, "foo"), 0, true, false)
canonicalize("stdout")
check(samefile("expected", "stdout"))

