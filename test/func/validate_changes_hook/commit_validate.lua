function validate_changes(revdata, branchname)
  local parsed = parse_basic_io(revdata)
  for _,stanza in ipairs(parsed) do
    if stanza.name == "add_file" or
       stanza.name == "patch" then
      local file = stanza.values[1]
      if not guess_binary_file_contents(file) then
        local fp = assert(io.open(file, "r"))
        local contents = fp:read("*all")
        fp:close()
        if string.find(contents, "\012\013") then
          return false, "CRLF detected"
        end
      end
    end
  end
  return true, ""
end
