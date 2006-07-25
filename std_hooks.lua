
-- this is the standard set of lua hooks for monotone;
-- user-provided files can override it or add to it.

function temp_file()
   local tdir
   tdir = os.getenv("TMPDIR")
   if tdir == nil then tdir = os.getenv("TMP") end
   if tdir == nil then tdir = os.getenv("TEMP") end
   if tdir == nil then tdir = "/tmp" end
   return mkstemp(string.format("%s/mtn.XXXXXX", tdir))
end

function execute(path, ...)   
   local pid
   local ret = -1
   pid = spawn(path, unpack(arg))
   if (pid ~= -1) then ret, pid = wait(pid) end
   return ret
end

-- Wrapper around execute to let user confirm in the case where a subprocess
-- returns immediately
-- This is needed to work around some brokenness with some merge tools
-- (e.g. on OS X)
function execute_confirm(path, ...)   
   ret = execute(path, unpack(arg))

   if (ret ~= 0)
   then
      print(gettext("Press enter"))
   else
      print(gettext("Press enter when the subprocess has completed"))
   end
   io.read()
   return ret
end

-- attributes are persistent metadata about files (such as execute
-- bit, ACLs, various special flags) which we want to have set and
-- re-set any time the files are modified. the attributes themselves
-- are stored in the roster associated with the revision. each (f,k,v)
-- attribute triple turns into a call to attr_functions[k](f,v) in lua.

if (attr_init_functions == nil) then
   attr_init_functions = {}
end

attr_init_functions["mtn:execute"] = 
   function(filename)
      if (is_executable(filename)) then 
        return "true" 
      else 
        return nil 
      end 
   end

attr_init_functions["mtn:manual_merge"] = 
   function(filename)
      if (binary_file(filename)) then 
        return "true" -- binary files must merged manually
      else 
        return nil
      end 
   end

if (attr_functions == nil) then
   attr_functions = {}
end

attr_functions["mtn:execute"] = 
   function(filename, value) 
      if (value == "true") then
         make_executable(filename)
      end
   end

function dir_matches(name, dir)
   -- helper for ignore_file, matching files within dir, or dir itself.
   -- eg for dir of 'CVS', matches CVS/, CVS/*, */CVS/ and */CVS/*
   if (string.find(name, "^" .. dir .. "/")) then return true end
   if (string.find(name, "^" .. dir .. "$")) then return true end
   if (string.find(name, "/" .. dir .. "/")) then return true end
   if (string.find(name, "/" .. dir .. "$")) then return true end
   return false
end

function ignore_file(name)
   -- project specific
   if (ignored_files == nil) then
      ignored_files = {}
      local ignfile = io.open(".mtn-ignore", "r")
      if (ignfile ~= nil) then
         local line = ignfile:read()
         while (line ~= nil) do
            table.insert(ignored_files, line)
            line = ignfile:read()
         end
         io.close(ignfile)
      end
   end
   for i, line in pairs(ignored_files)
   do
      local pcallstatus, result = pcall(function() return regex.search(line, name) end)
      if pcallstatus == true then
          -- no error from the regex.search call
          if result == true then return true end
      else
          -- regex.search had a problem, warn the user their .mtn-ignore file syntax is wrong
          io.stderr:write("WARNING: the line '" .. line .. "' in your .mtn-ignore file caused error '" .. result .. "'"
                           .. " while matching filename '" .. name .. "'.\nignoring this regex for all remaining files.\n")
          table.remove(ignored_files, i)
      end
   end

   local file_pats = {
      -- c/c++
      "%.a$", "%.so$", "%.o$", "%.la$", "%.lo$", "^core$",
      "/core$", "/core%.%d+$",
      -- java
      "%.class$",
      -- python
      "%.pyc$", "%.pyo$",
      -- TeX
      "%.aux$",
      -- backup files
      "%.bak$", "%.orig$", "%.rej$", "%~$",
      -- vim creates .foo.swp files
      "%.[^/]*%.swp$",
      -- emacs creates #foo# files
      "%#[^/]*%#$",
      -- other VCSes (where metadata is stored in named files):
      "%.scc$",
      -- desktop/directory configuration metadata
      "^.DS_Store$", "/.DS_Store$", "^desktop.ini$", "/desktop.ini$"
   }

   local dir_pats = {
      -- autotools detritus:
      "autom4te.cache", ".deps",
      -- Cons/SCons detritus:
      ".consign", ".sconsign",
      -- other VCSes (where metadata is stored in named dirs):
      "CVS", ".svn", "SCCS", "_darcs", ".cdv", ".git", ".bzr", ".hg"
   }

   for _, pat in ipairs(file_pats) do
      if string.find(name, pat) then return true end
   end
   for _, pat in ipairs(dir_pats) do
      if dir_matches(name, pat) then return true end
   end

   return false;
end

-- return true means "binary", false means "text",
-- nil means "unknown, try to guess"
function binary_file(name)
   -- some known binaries, return true
   local bin_pats = {
      "%.gif$", "%.jpe?g$", "%.png$", "%.bz2$", "%.gz$", "%.zip$",
      "%.class$", "%.jar$", "%.war$", "%.ear$"
   }

   -- some known text, return false
   local txt_pats = {
      "%.cc?$", "%.cxx$", "%.hh?$", "%.hxx$", "%.cpp$", "%.hpp$",
      "%.lua$", "%.texi$", "%.sql$", "%.java$"
   }

   local lowname=string.lower(name)
   for _, pat in ipairs(bin_pats) do
      if string.find(lowname, pat) then return true end
   end
   for _, pat in ipairs(txt_pats) do
      if string.find(lowname, pat) then return false end
   end

   -- unknown - read file and use the guess-binary 
   -- monotone built-in function
   return guess_binary_file_contents(name)
end

-- given a file name, return a regular expression which will match
-- lines that name top-level constructs in that file, or "", to disable
-- matching.
function get_encloser_pattern(name)
   -- texinfo has special sectioning commands
   if (string.find(name, "%.texi$")) then
      -- sectioning commands in texinfo: @node, @chapter, @top, 
      -- @((sub)?sub)?section, @unnumbered(((sub)?sub)?sec)?,
      -- @appendix(((sub)?sub)?sec)?, @(|major|chap|sub(sub)?)heading
      return ("^@("
              .. "node|chapter|top"
              .. "|((sub)?sub)?section"
              .. "|(unnumbered|appendix)(((sub)?sub)?sec)?"
              .. "|(major|chap|sub(sub)?)?heading"
              .. ")")
   end
   -- LaTeX has special sectioning commands.  This rule is applied to ordinary
   -- .tex files too, since there's no reliable way to distinguish those from
   -- latex files anyway, and there's no good pattern we could use for
   -- arbitrary plain TeX anyway.
   if (string.find(name, "%.tex$")
       or string.find(name, "%.ltx$")
       or string.find(name, "%.latex$")) then
      return ("\\\\("
              .. "part|chapter|paragraph|subparagraph"
              .. "|((sub)?sub)?section"
              .. ")")
   end
   -- There's no good way to find section headings in raw text, and trying
   -- just gives distracting output, so don't even try.
   if (string.find(name, "%.txt$")
       or string.upper(name) == "README") then
      return ""
   end
   -- This default is correct surprisingly often -- in pretty much any text
   -- written with code-like indentation.
   return "^[[:alnum:]$_]"
end

function edit_comment(basetext, user_log_message)
   local exe = nil
   if (program_exists_in_path("vi")) then exe = "vi" end
   if (program_exists_in_path("notepad.exe")) then exe = "notepad.exe" end
   local visual = os.getenv("VISUAL")
   if (visual ~= nil) then exe = visual end
   local editor = os.getenv("EDITOR")
   if (editor ~= nil) then exe = editor end

   if (exe == nil) then
      io.write("Could not find editor to enter commit message\n"
               .. "Try setting the environment variable EDITOR\n")
      return nil
   end

   local tmp, tname = temp_file()
   if (tmp == nil) then return nil end
   basetext = "MTN: " .. string.gsub(basetext, "\n", "\nMTN: ") .. "\n"
   tmp:write(user_log_message)
   if user_log_message == "" or string.sub(user_log_message, -1) ~= "\n" then
      tmp:write("\n")
   end
   tmp:write(basetext)
   io.close(tmp)

   if (execute(exe, tname) ~= 0) then
      io.write(string.format(gettext("Error running editor '%s' to enter log message\n"),
                             exe))
      os.remove(tname)
      return nil
   end

   tmp = io.open(tname, "r")
   if (tmp == nil) then os.remove(tname); return nil end
   local res = ""
   local line = tmp:read()
   while(line ~= nil) do 
      if (not string.find(line, "^MTN:")) then
         res = res .. line .. "\n"
      end
      line = tmp:read()
   end
   io.close(tmp)
   os.remove(tname)
   return res
end


function persist_phrase_ok()
   return true
end


function use_inodeprints()
   return false
end


-- trust evaluation hooks

function intersection(a,b)
   local s={}
   local t={}
   for k,v in pairs(a) do s[v] = 1 end
   for k,v in pairs(b) do if s[v] ~= nil then table.insert(t,v) end end
   return t
end

function get_revision_cert_trust(signers, id, name, val)
   return true
end

function get_manifest_cert_trust(signers, id, name, val)
   return true
end

function get_file_cert_trust(signers, id, name, val)
   return true
end

function accept_testresult_change(old_results, new_results)
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

-- merger support

function merge3_meld_cmd(lfile, afile, rfile)
   return 
   function()
      local path = "meld"
      local ret = execute(path, lfile, afile, rfile)
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), path))
      end
      return ret
   end
end

function merge3_tortoise_cmd(lfile, afile, rfile, outfile)
   return
   function()
      local path = "tortoisemerge"
      local ret = execute(path,
                          string.format("/base:%s", afile),
                          string.format("/theirs:%s", lfile),
                          string.format("/mine:%s", rfile),
                          string.format("/merged:%s", outfile))
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), path))
      end
      return ret
   end
end

function merge3_vim_cmd(vim, afile, lfile, rfile, outfile)
   return
   function()
      local ret = execute(vim, "-f", "-d", "-c", string.format("file %s", outfile),
                          afile, lfile, rfile)
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), vim))
      end
      return ret
   end
end

function merge3_rcsmerge_vim_cmd(merge, vim, lfile, afile, rfile, outfile)
   return
   function()
      -- XXX: This is tough - should we check if conflict markers stay or not?
      -- If so, we should certainly give the user some way to still force
      -- the merge to proceed since they can appear in the files (and I saw
      -- that). --pasky
      if execute(merge, lfile, afile, rfile) == 0 then
         copy_text_file(lfile, outfile);
         return 0
      end
      local ret = execute(vim, "-f", "-c", string.format("file %s", outfile),
                          lfile)
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), vim))
      end
      return ret
   end
end

function merge3_emacs_cmd(emacs, lfile, afile, rfile, outfile)
   local elisp = "(ediff-merge-files-with-ancestor \"%s\" \"%s\" \"%s\" nil \"%s\")"
   return 
   function()
      local ret = execute(emacs, "--eval", 
                          string.format(elisp, lfile, rfile, afile, outfile))
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), emacs))
      end
      return ret
   end
end

function merge3_xxdiff_cmd(left_path, anc_path, right_path, merged_path, 
                           lfile, afile, rfile, outfile)
   return 
   function()
      local path = "xxdiff"
      local ret = execute(path, 
                        "--title1", left_path,
                        "--title2", right_path,
                        "--title3", merged_path,
                        lfile, afile, rfile, 
                        "--merge", 
                        "--merged-filename", outfile,
                        "--exit-with-merge-status")
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), path))
      end
      return ret
   end
end
   
function merge3_kdiff3_cmd(left_path, anc_path, right_path, merged_path, 
                           lfile, afile, rfile, outfile)
   return 
   function()
      local path = "kdiff3"
      local ret = execute(path, 
                          "--L1", anc_path,
                          "--L2", left_path,
                          "--L3", right_path,
                          afile, lfile, rfile, 
                          "--merge", 
                          "--o", outfile)
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), path))
      end
      return ret
   end
end

function merge3_opendiff_cmd(left_path, anc_path, right_path, merged_path, lfile, afile, rfile, outfile)
   return 
   function()
      local path = "opendiff"
      -- As opendiff immediately returns, let user confirm manually
      local ret = execute_confirm(path,
                                  lfile,rfile,
                                  "-ancestor",afile,
                                  "-merge",outfile)
      if (ret ~= 0) then
         io.write(string.format(gettext("Error running merger '%s'\n"), path))
      end
      return ret
   end
end

function write_to_temporary_file(data)
   tmp, filename = temp_file()
   if (tmp == nil) then 
      return nil 
   end;
   tmp:write(data)
   io.close(tmp)
   return filename
end

function copy_text_file(srcname, destname)
   src = io.open(srcname, "r")
   if (src == nil) then return nil end
   dest = io.open(destname, "w")
   if (dest == nil) then return nil end

   while true do
      local line = src:read()
      if line == nil then break end
      dest:write(line, "\n")
   end

   io.close(dest)
   io.close(src)
end

function read_contents_of_file(filename, mode)
   tmp = io.open(filename, mode) 
   if (tmp == nil) then
      return nil
   end
   local data = tmp:read("*a")
   io.close(tmp)
   return data
end

function program_exists_in_path(program)
   return existsonpath(program) == 0
end

function get_preferred_merge3_command (tbl)
   local cmd = nil
   local left_path = tbl.left_path
   local anc_path = tbl.anc_path
   local right_path = tbl.right_path
   local merged_path = tbl.merged_path
   local lfile = tbl.lfile
   local afile = tbl.afile
   local rfile = tbl.rfile
   local outfile = tbl.outfile 

   local editor = os.getenv("EDITOR")
   if editor ~= nil then editor = string.lower(editor) else editor = "" end

   local merge = os.getenv("MTMERGE")
   -- TODO: Support for rcsmerge_emacs
   if merge ~= nil and string.find(editor, "vim") ~= nil then
      if os.getenv ("DISPLAY") ~= nil and program_exists_in_path ("gvim") then 
         cmd = merge3_rcsmerge_vim_cmd (merge, "gvim", lfile, afile, rfile, outfile) 
      elseif program_exists_in_path ("vim") then 
         cmd = merge3_rcsmerge_vim_cmd (merge, "vim", lfile, afile, rfile, outfile) 
      end

   elseif program_exists_in_path("kdiff3") then
      cmd = merge3_kdiff3_cmd (left_path, anc_path, right_path, merged_path, lfile, afile, rfile, outfile) 
   elseif program_exists_in_path ("xxdiff") then 
      cmd = merge3_xxdiff_cmd (left_path, anc_path, right_path, merged_path, lfile, afile, rfile, outfile) 
   elseif program_exists_in_path ("opendiff") then 
      cmd = merge3_opendiff_cmd (left_path, anc_path, right_path, merged_path, lfile, afile, rfile, outfile) 
   elseif program_exists_in_path ("TortoiseMerge") then
      cmd = merge3_tortoise_cmd(lfile, afile, rfile, outfile)
   elseif string.find(editor, "emacs") ~= nil or string.find(editor, "gnu") ~= nil then 
      if string.find(editor, "xemacs") and program_exists_in_path ("xemacs") then 
         cmd = merge3_emacs_cmd ("xemacs", lfile, afile, rfile, outfile) 
      elseif program_exists_in_path ("emacs") then 
         cmd = merge3_emacs_cmd ("emacs", lfile, afile, rfile, outfile) 
      end
   elseif string.find(editor, "vim") ~= nil then
      io.write (string.format("\nWARNING: 'vim' was choosen to perform external 3-way merge.\n"..
          "You should merge all changes to *LEFT* file due to limitation of program\n"..
          "arguments.  The order of the files is ancestor, left, right.\n\n")) 
      if os.getenv ("DISPLAY") ~= nil and program_exists_in_path ("gvim") then 
         cmd = merge3_vim_cmd ("gvim", afile, lfile, rfile, outfile) 
      elseif program_exists_in_path ("vim") then 
         cmd = merge3_vim_cmd ("vim", afile, lfile, rfile, outfile) 
      end
   elseif program_exists_in_path ("meld") then 
      tbl.meld_exists = true 
      io.write (string.format("\nWARNING: 'meld' was choosen to perform external 3-way merge.\n"..
          "You should merge all changes to *CENTER* file due to limitation of program\n"..
          "arguments.\n\n")) 
      cmd = merge3_meld_cmd (lfile, afile, rfile) 
   end 
   
   return cmd 
end 

function merge3 (anc_path, left_path, right_path, merged_path, ancestor, left, right) 
   local ret 
   local tbl = {}
   
   tbl.anc_path = anc_path 
   tbl.left_path = left_path 
   tbl.right_path = right_path 

   tbl.merged_path = merged_path 
   tbl.afile = nil 
   tbl.lfile = nil 
   tbl.rfile = nil 
   tbl.outfile = nil 
   tbl.meld_exists = false 
   tbl.lfile = write_to_temporary_file (left) 
   tbl.afile =   write_to_temporary_file (ancestor) 
   tbl.rfile =   write_to_temporary_file (right) 
   tbl.outfile = write_to_temporary_file ("") 
   
   if tbl.lfile ~= nil and tbl.rfile ~= nil and tbl.afile ~= nil and tbl.outfile ~= nil 
   then 
      local cmd =   get_preferred_merge3_command (tbl) 
      if cmd ~=nil 
      then 
         io.write (string.format(gettext("executing external 3-way merge command\n")))
         -- cmd() return 0 on success.
         if cmd () ~= 0
         then
            ret = nil
         else
            if tbl.meld_exists 
            then 
               ret = read_contents_of_file (tbl.afile, "r")
            else
               ret = read_contents_of_file (tbl.outfile, "r") 
            end 
            if string.len (ret) == 0 
            then 
               ret = nil 
            end
         end
      else
         io.write (string.format("No external 3-way merge command found.\n"..
            "You may want to check that $EDITOR is set to an editor that supports 3-way merge,\n"..
            "set this explicitly in your get_preferred_merge3_command hook,\n"..
            "or add a 3-way merge program to your path.\n\n"))
      end
   end
   
   os.remove (tbl.lfile)
   os.remove (tbl.rfile)
   os.remove (tbl.afile)
   os.remove (tbl.outfile)
   
   return ret
end 

-- expansion of values used in selector completion

function expand_selector(str)

   -- something which looks like a generic cert pattern
   if string.find(str, "^[^=]*=.*$")
   then
      return ("c:" .. str)
   end

   -- something which looks like an email address
   if string.find(str, "[%w%-_]+@[%w%-_]+")
   then
      return ("a:" .. str)
   end

   -- something which looks like a branch name
   if string.find(str, "[%w%-]+%.[%w%-]+")
   then
      return ("b:" .. str)
   end

   -- a sequence of nothing but hex digits
   if string.find(str, "^%x+$")
   then
      return ("i:" .. str)
   end

   -- tries to expand as a date
   local dtstr = expand_date(str)
   if  dtstr ~= nil
   then
      return ("d:" .. dtstr)
   end
   
   return nil
end

-- expansion of a date expression
function expand_date(str)
   -- simple date patterns
   if string.find(str, "^19%d%d%-%d%d")
      or string.find(str, "^20%d%d%-%d%d")
   then
      return (str)
   end

   -- "now" 
   if str == "now"
   then
      local t = os.time(os.date('!*t'))
      return os.date("%FT%T", t)
   end
   
   -- today don't uses the time         # for xgettext's sake, an extra quote
   if str == "today"
   then
      local t = os.time(os.date('!*t'))
      return os.date("%F", t)
   end
   
   -- "yesterday", the source of all hangovers
   if str == "yesterday"
   then
      local t = os.time(os.date('!*t'))
      return os.date("%F", t - 86400)
   end
   
   -- "CVS style" relative dates such as "3 weeks ago"
   local trans = { 
      minute = 60; 
      hour = 3600; 
      day = 86400; 
      week = 604800; 
      month = 2678400; 
      year = 31536000 
   }
   local pos, len, n, type = string.find(str, "(%d+) ([minutehordaywk]+)s? ago")
   if trans[type] ~= nil
   then
      local t = os.time(os.date('!*t'))
      if trans[type] <= 3600
      then
        return os.date("%FT%T", t - (n * trans[type]))
      else      
        return os.date("%F", t - (n * trans[type]))
      end
   end
   
   return nil
end


external_diff_default_args = "-u"

-- default external diff, works for gnu diff
function external_diff(file_path, data_old, data_new, is_binary, diff_args, rev_old, rev_new)
   local old_file = write_to_temporary_file(data_old);
   local new_file = write_to_temporary_file(data_new);

   if diff_args == nil then diff_args = external_diff_default_args end
   execute("diff", diff_args, "--label", file_path .. "\told", old_file, "--label", file_path .. "\tnew", new_file);

   os.remove (old_file);
   os.remove (new_file);
end

-- netsync permissions hooks (and helper)

function globish_match(glob, str)
      local pcallstatus, result = pcall(function() if (globish.match(glob, str)) then return true else return false end end)
      if pcallstatus == true then
          -- no error
          return result
      else
          -- globish.match had a problem
          return nil
      end
end

function get_netsync_read_permitted(branch, ident)
   local permfile = io.open(get_confdir() .. "/read-permissions", "r")
   if (permfile == nil) then return false end
   local dat = permfile:read("*a")
   io.close(permfile)
   local res = parse_basic_io(dat)
   if res == nil then
      io.stderr:write("file read-permissions cannot be parsed\n")
      return false
   end
   local matches = false
   local cont = false
   for i, item in pairs(res)
   do
      -- legal names: pattern, allow, deny, continue
      if item.name == "pattern" then
         if matches and not cont then return false end
         matches = false
         cont = false
         for j, val in pairs(item.values) do
            if globish_match(val, branch) then matches = true end
         end
      elseif item.name == "allow" then if matches then
         for j, val in pairs(item.values) do
            if val == "*" then return true end
            if val == "" and ident == nil then return true end
            if globish_match(val, ident) then return true end
         end
      end elseif item.name == "deny" then if matches then
         for j, val in pairs(item.values) do
            if val == "*" then return false end
            if val == "" and ident == nil then return false end
            if globish_match(val, ident) then return false end
         end
      end elseif item.name == "continue" then if matches then
         cont = true
         for j, val in pairs(item.values) do
            if val == "false" or val == "no" then cont = false end
         end
      end elseif item.name ~= "comment" then
         io.stderr:write("unknown symbol in read-permissions: " .. item.name .. "\n")
         return false
      end
   end
   return false
end

function get_netsync_write_permitted(ident)
   local permfile = io.open(get_confdir() .. "/write-permissions", "r")
   if (permfile == nil) then
      return false
   end
   local matches = false
   local line = permfile:read()
   while (not matches and line ~= nil) do
      local _, _, ln = string.find(line, "%s*([^%s]*)%s*")
      if ln == "*" then matches = true end
      if globish_match(ln, ident) then matches = true end
      line = permfile:read()
   end
   io.close(permfile)
   return matches
end

-- This is a simple funciton which assumes you're going to be spawning
-- a copy of mtn, so reuses a common bit at the end for converting
-- local args into remote args. You might need to massage the logic a
-- bit if this doesn't fit your assumptions.

function get_netsync_connect_command(uri, args)

        local argv = nil
        local quote_patterns = false

        if uri["scheme"] == "ssh" 
                and uri["host"] 
                and uri["path"] then

                argv = { "ssh" }
                if uri["user"] then
                        table.insert(argv, "-l")
                        table.insert(argv, uri["user"])
                end
                if uri["port"] then
                        table.insert(argv, "-p")
                        table.insert(argv, uri["port"])
                end

                table.insert(argv, uri["host"])
    quote_patterns = true
        end
        
        if uri["scheme"] == "file" and uri["path"] then
                argv = { }
        end

        if argv then

                table.insert(argv, get_mtn_command(uri["host"]))

                if args["debug"] then
                        table.insert(argv, "--debug")
                else
                        table.insert(argv, "--quiet")
                end

                table.insert(argv, "--db")
                table.insert(argv, uri["path"])
                table.insert(argv, "serve")
                table.insert(argv, "--stdio")
                table.insert(argv, "--no-transport-auth")

                -- patterns must be quoted to avoid a remote shell expanding them
                if args["include"] then
                        local include = args["include"]
                        if quote_patterns then
                                include = "'" .. args["include"] .. "'"
                        end
                        table.insert(argv, include)
                end

                if args["exclude"] then
                        table.insert(argv, "--exclude")
                        local exclude = args["exclude"]
                        if quote_patterns then
                                exclude = "'" .. args["exclude"] .. "'"
                        end
                        table.insert(argv, exclude)
                end
        end
        return argv
end

function use_transport_auth(uri)
        if uri["scheme"] == "ssh" 
        or uri["scheme"] == "file" then
                return false
        else
                return true
        end
end

function get_mtn_command(host)
        return "mtn"
end
