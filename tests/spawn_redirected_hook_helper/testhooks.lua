attr_init_functions["magic"] = function (name)
   ok = true
   if existsonpath("touch") == 0 then
      pid = spawn_redirected ("", "", "xyzzy", "touch", "foofile")
      if pid == -1 then ok = false end
   elseif existsonpath("xcopy") == 0 then
      pid = spawn_redirected ("", "", "xyzzy", "xcopy")
      if pid == -1 then ok = false end
   else
      x = io.open("skipfile", "w")
      x:close()
   end

  if ok then
    x = io.open("outfile", "w")
    x:close()
  end

  return nil
end
