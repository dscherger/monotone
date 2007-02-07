mtn_setup()
revs = {}

get("expected")

writefile("empty", "")

addfile("foo", "blah")
check(mtn("commit", "--date=2005-05-21T12:30:51", "--branch=testbranch",
          "--message=blah-blah"), 0, false, false)
base = base_revision()

check(mtn("automate", "cert", base, "testcert", "testvalue"), 0, true, false)
check(samefile("empty", "stdout"))

-- check that a correct usage produces correctly formatted output
check(mtn("automate", "certs", base), 0, true, false)
canonicalize("stdout")
check(samefile("expected", "stdout"))
