-- Functions useful with parse_basic_io

function checkexp (label, computed, expected, xfail)
-- Throw an error with a helpful message if 'computed' doesn't equal
-- 'expected'.
   if computed ~= expected then
      if xfail then
         err(false, 2)
      else
         err (label .. " Expected '" .. expected .. "' got '" .. computed .. "'")
      end
   end
end

function check_basic_io_line (label, computed, name, value, xfail)
-- Compare a parsed basic_io line 'computed' to 'name', 'value', throw
-- an error (with a helpful message) if they don't match.
   checkexp(label .. ".name", computed.name, name, xfail)

   if type(value) == "table" then
      checkexp(label .. ".length", #computed.values, #value, xfail)
      for i = 1, #value do
         checkexp(label .. i, computed.values[i], value[i], xfail)
      end

   else
      checkexp(label .. ".length", #computed.values, 1, xfail)
      checkexp(label .. "." .. name, computed.values[1], value, xfail)
   end
end

function find_basic_io_line (parsed, line)
-- return index in parsed (output of parse_basic_io) matching
-- line.name, line.values
   for I = 1, #parsed do
       if parsed[I].name == line.name then
          if type (line.values) ~= "table" then
             if parsed[I].values[1] == line.values then
                return I
             end
          else
             err ("searching for line with table of values not yet supported")
          end
       end
   end

   err ("line '" .. line.name .. " " .. line.values .. "' not found")
end

-- end of file
