-- Create some revisions then attach 'testresult' certs to them.
-- `mtn update` must not update to a "fail" revision from a "pass"
-- revision.

-- This is handled by our default definition of 'accept_testresult_change'
-- which reads '_MTN/wanted-testresults'.

mtn_setup()

addfile("numbers.txt", 1)
commit()
good_rev = base_revision()

writefile("numbers.txt", 2)
commit()
bad_rev = base_revision()

check(mtn("update", "-r", good_rev), 0, false, false)

check(mtn("testresult", good_rev, "pass"), 0, false, false)
check(mtn("testresult", bad_rev, "fail"), 0, false, false)

-- Now write out our default 'accept_testresult_change' definition
-- from 'std_hooks.lua'.
writefile("_MTN/monotonerc", [[
-- http://snippets.luacode.org/?p=snippets/String_to_Hex_String_68
function hex_dump(str,spacer)
   return (string.gsub(str,"(.)",
      function (c)
         return string.format("%02x%s",string.byte(c), spacer or "")
      end)
   )
end

function accept_testresult_change_hex(old_results, new_results)
   local reqfile = io.open("_MTN/wanted-testresults", "r")
   if (reqfile == nil) then return true end
   local line = reqfile:read()
   local required = {}
   while (line ~= nil)
   do
      required[line] = true
      line = reqfile:read()
   end
   io.close(reqfile)
   for test, res in pairs(required)
   do
      if old_results[test] == true and new_results[test] ~= true
      then
         return false
      end
   end
   return true
end

function accept_testresult_change(old_results, new_results)
   -- Hex encode each of the key hashes to match those in 'wanted-testresults'
   local old_results_hex = {}
   for k, v in pairs(old_results) do
	old_results_hex[hex_dump(k)] = v
   end

   local new_results_hex = {}
   for k, v in pairs(new_results) do
      new_results_hex[hex_dump(k)] = v
   end

   return accept_testresult_change_hex(old_results_hex, new_results_hex)
end
]])
writefile("_MTN/wanted-testresults", "46ec58576f9e4f34a9eede521422aa5fd299dc50\n")

check(mtn("update", "--rcfile", "_MTN/monotonerc"), 0, false, true)
-- stderr now looks something like.
-- mtn: updating along branch '$BRANCH'
-- mtn: already up to date at $good_rev
check(qgrep(good_rev, "stderr"))
check(base_revision() == good_rev)

