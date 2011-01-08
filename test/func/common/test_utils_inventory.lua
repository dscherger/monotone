-- Functions useful in tests/automate_inventory*
-- uses basic_io.lua

function xfail_inventory (parsed, parsed_index, stanza)
   return check_inventory(parsed, parsed_index, stanza, true)
end

function check_inventory (parsed, parsed_index, stanza, xfail)
-- 'stanza' is a table for one stanza
-- 'parsed_index' gives the first index for this stanza in 'parsed'
-- (which should be the output of parse_basic_io).
-- Compare 'stanza' to 'parsed'; fail if different.
-- Returns parsed_index incremented to the next index to check.

   -- we assume that any test failure is not an expected failure if not
   -- otherwise given
   if xfail == nil then xfail = false end

   check_basic_io_line (parsed_index, parsed[parsed_index], "path", stanza.path, xfail)
   parsed_index = parsed_index + 1

   if stanza.old_type then
      check_basic_io_line (parsed_index, parsed[parsed_index], "old_type", stanza.old_type, xfail)
      parsed_index = parsed_index + 1
   end

   if stanza.new_path then
      check_basic_io_line (parsed_index, parsed[parsed_index], "new_path", stanza.new_path, xfail)
      parsed_index = parsed_index + 1
   end

   if stanza.new_type then
      check_basic_io_line (parsed_index, parsed[parsed_index], "new_type", stanza.new_type, xfail)
      parsed_index = parsed_index + 1
   end

   if stanza.old_path then
      check_basic_io_line (parsed_index, parsed[parsed_index], "old_path", stanza.old_path, xfail)
      parsed_index = parsed_index + 1
   end

   if stanza.fs_type then
      check_basic_io_line (parsed_index, parsed[parsed_index], "fs_type", stanza.fs_type, xfail)
      parsed_index = parsed_index + 1
   end

   if stanza.birth then
      check_basic_io_line (parsed_index, parsed[parsed_index], "birth", stanza.birth, xfail)
      parsed_index = parsed_index + 1
   end

   if stanza.status then
      check_basic_io_line (parsed_index, parsed[parsed_index], "status", stanza.status, xfail)
      parsed_index = parsed_index + 1
   end

   if stanza.changes then
      check_basic_io_line (parsed_index, parsed[parsed_index], "changes", stanza.changes, xfail)
      parsed_index = parsed_index + 1
   end

   return parsed_index
end -- check_inventory

-- end of file
