
mtn_setup()

function chk()
  check(mtn("automate", "get-current-revision-id"), 0, true, false)
  rename("stdout", "current")
  check(mtn("automate", "get-revision"), 0, true)
  check(mtn("identify"), 0, true, nil, {"stdout"})
  check(trim(readfile("current")) == trim(readfile("stdout")))
end

-- check an empty base revision id

chk()

-- check pending changes against an empty base

addfile("foo", "this is file foo")

chk()

commit()

-- check no changes against a non-empty base

chk()

-- check changes against a non-empty base

addfile("bar", "this is file bar")

chk()
