
include("/common/netsync.lua")
mtn_setup()
netsync.setup()

srv = netsync.start(2)

check(mtn("genkey", "committer@test.net"), 0, false, false,
       "committer@test.net\ncommitter@test.net\n")

writefile("testfile", "version 0 of test file")
check(mtn("add", "testfile"), 0, false, false)
check(mtn("ci", "-mx", "-k", "committer@test.net"), 0, false, false)

check(mtn("au", "select", "b:testbranch"), 0, true)

srv:push("testbranch", 1)


check(mtn("dropkey", "committer@test.net"), 0, false, false)
check(mtn("genkey", "committer@test.net"), 0, false, false,
       "committer@test.net\ncommitter@test.net\n")

writefile("testfile", "version 1 of test file")
check(mtn("ci", "-mx", "-k", "committer@test.net"), 0, false, false)

srv:push("testbranch", 1, 0)

srv:stop()

check(mtn2("ls", "keys"), 0, true, true)

key_lines = readfile_lines("stdout")
first_line = nil
have_different = false
for _, line in pairs(key_lines) do
   if string.find(line, "committer@test.net") then
      if first_line == nil then
         first_line = line
      elseif first_line ~= line then
         have_different = true
      end
   end
end
check(have_different)

check(qgrep("multiple keys", "stderr"))
