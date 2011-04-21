includecommon("automate_stdio.lua")

mtn_setup()


function dotest(main_options, subcmd_options, have_p, have_w, have_debug)
   local thecmd = mtn("automate", "stdio")
   for _, opt in ipairs(main_options) do
      table.insert(thecmd, opt)
   end
   check(thecmd, 0, true, true,
	 make_stdio_cmd({"bandtest", "info"}, subcmd_options) ..
	 make_stdio_cmd({"bandtest", "warning"}, subcmd_options) ..
      make_stdio_cmd({"bandtest", "error"}, subcmd_options))
   data = readfile("stdout")
   check((parse_stdio(data, 0, 0, "p")[1] ~= nil) == have_p)
   check((parse_stdio(data, 0, 1, "w")[1] ~= nil) == have_w)
   check(parse_stdio(data, 2, 2, "e")[1] ~= nil)
   local err = readfile("stderr")
   if have_debug then
      check(err:find("running bandtest info") ~= nil)
      check(err:find("running bandtest warning") ~= nil)
      check(err:find("running bandtest error") ~= nil)
   else
      check(err == "")
   end
end

dotest({}, {}, true, true, false)
dotest({}, {{"v", ""}}, true, true, false)
dotest({"--quiet"}, {{"v", ""}}, true, true, false)
dotest({"--quiet"}, {}, false, true, false)
dotest({"-q", "-q"}, {}, false, false, false)

dotest({"--v"}, {}, true, true, true)
dotest({"--v"}, {{"q", ""}}, true, true, true)
dotest({"--v"}, {{"q", ""}, {"q", ""}}, false, true, true)
dotest({"--v"}, {{"q", ""}, {"q", ""}, {"q", ""}}, false, false, true)