
mtn_setup()

contents = "blah\n"

writefile("expected", contents)

check(mtn("automate", "put_file", contents), 0, true, false)
canonicalize("stdout")
file = "4cbd040533a2f43fc6691d773d510cda70f4126a"
writefile("fileid", file)
check(samefile("fileid", "stdout"))

-- check that a correct usage produces correctly formatted output
check(mtn("automate", "get_file", file), 0, true, false)
canonicalize("stdout")
check(samefile("expected", "stdout"))
