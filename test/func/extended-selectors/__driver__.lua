-- selector functions are:
--   difference(a,b)
--   not(a)
--   lca(a,b)
--   max(a)
--   ancestors(a)
--   descendants(a)
--   parents(a)
--   children(a)
-- operators are a/b/c for 'and' and a|b|c for 'or'

mtn_setup()

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

root = ci("testbranch", nil, "Joe")
lhs = ci("testbranch", root, "Joe")
rhs = ci("testbranch", root, "Anne")
m = merge(lhs, rhs, "testbranch", "Anne")
approve("otherbranch", lhs)
other = ci("otherbranch", lhs, "Jim")
other_2 = ci("otherbranch", other, "Jim")

expect("b:testbranch", root, lhs, rhs, m)
expect("b:otherbranch", lhs, other, other_2)
expect("b:testbranch/b:otherbranch", lhs)
expect("b:testbranch|b:otherbranch", root, lhs, rhs, m, other, other_2)

-- now do same tests again with a double not - should get same results
expect("not(not(b:testbranch))", root, lhs, rhs, m)
expect("not(not(b:otherbranch))", lhs, other, other_2)
expect("not(not(b:testbranch/b:otherbranch))", lhs)
expect("not(not(b:testbranch|b:otherbranch))", root, lhs, rhs, m, other, other_2)

expect("not(b:otherbranch)", root, rhs, m)
expect("not(b:testbranch)", other, other_2)
expect("not(h:testbranch)", root, lhs, rhs, other, other_2)
expect("not(lca(h:testbranch;h:otherbranch))", root, rhs, m, other, other_2)
expect("b:testbranch/not(a:Joe)", rhs, m)

expect("lca(h:testbranch;h:otherbranch)", lhs)
expect("max(b:testbranch/a:Joe)", lhs)
expect("max(b:otherbranch/a:Anne)")
expect("difference(b:testbranch;a:Joe)", rhs, m)
expect("ancestors(b:otherbranch)", other, lhs, root)
expect("descendants("..lhs..")", m, other, other_2)
expect("parents(lca(h:otherbranch;h:testbranch))", root)
expect("children(lca(h:otherbranch;h:testbranch))", m, other)

other_head = merge(other_2, m, "otherbranch", "Jack")
expect("max((ancestors(h:testbranch)|h:testbranch)/(ancestors(h:otherbranch)|h:otherbranch))", m)
expect("lca(h:testbranch;h:otherbranch)", m)


check(mtn("automate", "select", "pick(h:*)"), 0, true, nil)
numlines = 0
for line in io.lines("stdout")
do
   numlines = numlines + 1
   check(line == m or line == other_2)
end
check(numlines == 1)
