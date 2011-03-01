includecommon("selectors.lua")
mtn_setup()

addfile("testfile", "blah blah")
commit("testbranch", "this is revision one")
REV1=base_revision()

writefile("testfile", "stuff stuff")
commit("testbranch", "this is revision number two")
REV2=base_revision()

writefile("testfile", "foobar")
commit("testbranch", "this is revision number three")
REV3=base_revision()

check(mtn("comment", REV2, "revision 2 comment"), 0, true, false)

-- changelogs

selmap("m:*one*", {REV1})
selmap("m:*two*", {REV2})
selmap("m:*three*", {REV3})
selmap("m:*revision*", {REV1, REV2, REV3})
selmap("m:*number*", {REV2, REV3})
selmap("m:*foobar*", {})

-- comments

selmap("m:revision*", {REV2})
selmap("m:*2*", {REV2})
selmap("m:*comment*", {REV2})
