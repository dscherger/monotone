-- Demonstrate --move-conflicting-paths

mtn_setup()

addfile("somefile", "somefile content")
mkdir("somedir")
addfile("somedir/anotherfile", "anotherfile content")
commit("testbranch", "base")
base = base_revision()

addfile("thirdfile", "thirdfile content")
addfile("somedir/fourthfile", "fourthfile content")
commit("testbranch", "one")

revert_to(base)
writefile("thirdfile", "thirdfile content 2")
writefile("somedir/fourthfile", "fourthfile content 2")

-- reports conflicts with unversioned files
check(mtn("update"), 1, nil, true)
check(qgrep("unversioned", "stderr"))

-- moves them out of the way
check(mtn("update", "--move-conflicting-paths"), 0, nil, true)
check(qgrep("moved conflicting", "stderr"))

-- end of file
