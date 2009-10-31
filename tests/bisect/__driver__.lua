mtn_setup()

addfile("head1", "111 head")
commit("testbranch", "head1")
head1 = base_revision()

addfile("head2", "222 head")
commit("testbranch", "head2")
head2 = base_revision()

addfile("head3", "333 head")
commit("testbranch", "head3")
head3 = base_revision()

addfile("head4", "444 head")
commit("testbranch", "head4")
head4 = base_revision()


addfile("left1", "111 left")
commit("testbranch", "left1")
left1 = base_revision()

addfile("left2", "222 left")
commit("testbranch", "left2")
left2 = base_revision()

addfile("left3", "333 left")
commit("testbranch", "left3")
left3 = base_revision()

addfile("left4", "444 left")
commit("testbranch", "left4")
left4 = base_revision()


revert_to(head4)


addfile("right1", "111 right")
commit("testbranch", "right1")
right1 = base_revision()

addfile("right2", "222 right")
commit("testbranch", "right2")
right2 = base_revision()

addfile("right3", "333 right")
commit("testbranch", "right3")
right3 = base_revision()

addfile("right4", "444 right")
commit("testbranch", "right4")
right4 = base_revision()


check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)


addfile("tail1", "111 tail")
commit("testbranch", "tail1")
tail1 = base_revision()

addfile("tail2", "222 tail")
commit("testbranch", "tail2")
tail2 = base_revision()

addfile("tail3", "333 tail")
commit("testbranch", "tail3")
tail3 = base_revision()

addfile("tail4", "444 tail")
commit("testbranch", "tail4")
tail4 = base_revision()


check(mtn("log", "--no-files", "--from", head1, "--next", 20), 0, false, false)


-- test 1: addition of head3 is considered to be the error

check(exists("head3"))
check(mtn("bisect", "bad"), 0, false, false)
check(mtn("bisect", "good", "--revision", head1), 0, false, false)

while rev ~= base_revision() do
   rev = base_revision()
   if exists("head3") then
      check(mtn("bisect", "bad"), 0, false, false)
   else
      check(mtn("bisect", "good"), 0, false, false)
   end
end

check(base_revision() == head3)

-- repeatedly saying bad doesn't change anything

check(exists("head3"))
check(mtn("bisect", "bad"), 0, false, false)

check(base_revision() == head3)

check(exists("head3"))
check(mtn("bisect", "bad"), 0, false, false)

check(base_revision() == head3)

-- claiming this is now good causes the bisection to fail

check(mtn("bisect", "good"), 1, false, false)

check(base_revision() == head3)


-- test 2: addition of left2 is considered to be the error

check(mtn("bisect", "reset"), 0, false, false)
check(mtn("update"), 0, false, false)
rev = base_revision()

check(exists("left2"))
check(mtn("bisect", "good", "--revision", head1), 0, false, false)
check(mtn("bisect", "bad"), 0, false, false)

while rev ~= base_revision() do
   rev = base_revision()
   if exists("left2") then
      check(mtn("bisect", "bad"), 0, false, false)
   else
      check(mtn("bisect", "good"), 0, false, false)
   end
end

check(base_revision() == left2)


-- test 3: addition of right3 is considered to be the error

check(mtn("bisect", "reset"), 0, false, false)
check(mtn("update"), 0, false, false)
rev = base_revision()

check(exists("right3"))
check(mtn("bisect", "good", "--revision", head1), 0, false, false)
check(mtn("bisect", "bad"), 0, false, false)

while rev ~= base_revision() do
   rev = base_revision()
   if exists("right3") then
      check(mtn("bisect", "bad"), 0, false, false)
   else
      check(mtn("bisect", "good"), 0, false, false)
   end
end

check(base_revision() == right3)


-- test 4: addition of tail1 is considered to be the error

check(mtn("bisect", "reset"), 0, false, false)
check(mtn("update"), 0, false, false)
rev = base_revision()

check(exists("tail1"))
check(mtn("bisect", "good", "--revision", head1), 0, false, false)
check(mtn("bisect", "bad"), 0, false, false)

while rev ~= base_revision() do
   rev = base_revision()
   if exists("tail1") then
      check(mtn("bisect", "bad"), 0, false, false)
   else
      check(mtn("bisect", "good"), 0, false, false)
   end
end

check(base_revision() == tail1)
