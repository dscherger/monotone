-- See issue 176 - https://code.monotone.ca/p/monotone/issues/176/
-- It reports parentheses being mismatched when they actually aren't.
--
-- The example given used the parentheses to combine multiple
-- selectors with |, where A, B, and C represent other selectors.
-- eg. mtn au select (A|B)/C
-- However, the problem could still be reproduced with
-- mtn au select (A)/C
mtn_setup()

-- Setup some helper functions for creating dummy data
do
   local num = 0
   function ci(branch, parent, author)
      num = num + 1
      if parent ~= nil
      then
	 check(mtn("up", "-r", parent), 0, nil, false)
      end
      addfile("file-" .. num, "foo")
      check(mtn("commit", "-b", branch, "--author", author, "-mx"), 0, nil, false)
      return base_revision()
   end
   function merge(rev1, rev2, branch, author)
      check(mtn("explicit_merge", rev1, rev2, branch, "--author", author), 0, nil, true)
      local result = readfile("stderr"):match("%[merged%] (%x+)")
      L("Merge result: " .. result .. "\n")
      check(result:len() == 40)
      return result
   end
   function expect(selector, ...)
      check(mtn("automate", "select", selector), 0, true, nil)
      local linecount = 0;
      local expected = {...}
      local ok = true
      for line in io.lines("stdout")
      do
	 linecount = linecount + 1
	 local idx = nil
	 for k,v in ipairs(expected)
	 do
	    if v == line then idx = k end
	 end
	 if idx == nil
	 then
	    L("Did not expect " .. line .. "\n")
	    ok = false
	 else
	    expected[idx] = true
	 end
      end
      for k,v in ipairs(expected)
      do
	 if v ~= true
	 then
	    L("Expected " .. v .. "\n")
	    ok = false
	 end
      end
      check(ok)
   end
   function approve(branch, rev)
      check(mtn("approve", "-b", branch, rev), 0, nil, false)
   end
end

-- Create dummy data
root = ci("testbranch", nil, "Joe")
lhs = ci("testbranch", root, "Joe")
rhs = ci("testbranch", root, "Anne")
m = merge(lhs, rhs, "testbranch", "Anne")
approve("otherbranch", lhs)
other = ci("otherbranch", lhs, "Jim")
other_2 = ci("otherbranch", other, "Jim")

-- Test reported example where parentheses are used for grouping multiple selectors
expect("(a:Joe|a:Anne)/b:testbranch", root, lhs, rhs, m)
-- Simplified case using single selector
expect("(a:Joe)/b:testbranch", root, lhs)
-- Removing the parentheses for the simplified case should be a no-op
expect("a:Joe/b:testbranch", root, lhs)

-- Now lets swap the selectors around - should still work and get same data
expect("b:testbranch/(a:Joe|a:Anne)", root, lhs, rhs, m)
expect("b:testbranch/(a:Joe)", root, lhs)
expect("b:testbranch/a:Joe", root, lhs)
