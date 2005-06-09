
-- this is the standard set of lua hooks for monotone;
-- user-provided files can override it or add to it.

function temp_file()
   local tdir
   tdir = os.getenv("TMPDIR")
   if tdir == nil then tdir = os.getenv("TMP") end
   if tdir == nil then tdir = os.getenv("TEMP") end
   if tdir == nil then tdir = "/tmp" end
   return mkstemp(string.format("%s/mt.XXXXXX", tdir))
end

function execute(path, ...)   
   local pid
   local ret = -1
   pid = spawn(path, unpack(arg))
   if (pid ~= -1) then ret, pid = wait(pid) end
   return ret
end



-- attributes are persistent metadata about files (such as execute
-- bit, ACLs, various special flags) which we want to have set and
-- re-set any time the files are modified. the attributes themselves
-- are stored in a file .mt-attrs, in the working copy (and
-- manifest). each (f,k,v) triple in an attribute file turns into a
-- call to attr_functions[k](f,v) in lua.

if (attr_init_functions == nil) then
   attr_init_functions = {}
end

attr_init_functions["execute"] = 
   function(filename)
      if (is_executable(filename)) then 
        return "true" 
      else 
        return nil 
      end 
   end

attr_init_functions["manual_merge"] = 
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

attr_functions["execute"] = 
   function(filename, value) 
      if (value == "true") then
         make_executable(filename)
      end
   end


function ignore_file(name)
   -- c/c++
   if (string.find(name, "%.a$")) then return true end
   if (string.find(name, "%.so$")) then return true end
   if (string.find(name, "%.o$")) then return true end
   if (string.find(name, "%.la$")) then return true end
   if (string.find(name, "%.lo$")) then return true end
   if (string.find(name, "^core$")) then return true end
   if (string.find(name, "/core$")) then return true end
   -- python
   if (string.find(name, "%.pyc$")) then return true end
   if (string.find(name, "%.pyo$")) then return true end
   -- TeX
   if (string.find(name, "%.aux$")) then return true end
   -- backup files
   if (string.find(name, "%.bak$")) then return true end
   if (string.find(name, "%.orig$")) then return true end
   if (string.find(name, "%.rej$")) then return true end
   if (string.find(name, "%~$")) then return true end
   -- autotools detritus:
   if (string.find(name, "^autom4te.cache/")) then return true end
   if (string.find(name, "/autom4te.cache/")) then return true end
   if (string.find(name, "^.deps/")) then return true end
   if (string.find(name, "/.deps/")) then return true end
   -- other VCSes:
   if (string.find(name, "^CVS/")) then return true end
   if (string.find(name, "/CVS/")) then return true end
   if (string.find(name, "^%.svn/")) then return true end
   if (string.find(name, "/%.svn/")) then return true end
   if (string.find(name, "^SCCS/")) then return true end
   if (string.find(name, "/SCCS/")) then return true end
   if (string.find(name, "^_darcs/")) then return true end
   if (string.find(name, "^.cdv/")) then return true end
   if (string.find(name, "^.git/")) then return true end
   return false;
end

-- return true means "binary", false means "text",
-- nil means "unknown, try to guess"
function binary_file(name)
   local lowname=string.lower(name)
   -- some known binaries, return true
   if (string.find(lowname, "%.gif$")) then return true end
   if (string.find(lowname, "%.jpe?g$")) then return true end
   if (string.find(lowname, "%.png$")) then return true end
   if (string.find(lowname, "%.bz2$")) then return true end
   if (string.find(lowname, "%.gz$")) then return true end
   if (string.find(lowname, "%.zip$")) then return true end
   -- some known text, return false
   if (string.find(lowname, "%.cc?$")) then return false end
   if (string.find(lowname, "%.cxx$")) then return false end
   if (string.find(lowname, "%.hh?$")) then return false end
   if (string.find(lowname, "%.hxx$")) then return false end
   if (string.find(lowname, "%.lua$")) then return false end
   if (string.find(lowname, "%.texi$")) then return false end
   if (string.find(lowname, "%.sql$")) then return false end
   -- unknown - read file and use the guess-binary 
   -- monotone built-in function
   filedata=read_contents_of_file(name, "rb")
   if (filedata ~= nil) then return guess_binary(filedata) end
   -- if still unknown, treat as binary
   return true
end

function edit_comment(basetext, user_log_message)
   local exe = "vi"
   local visual = os.getenv("VISUAL")
   if (visual ~= nil) then exe = visual end
   local editor = os.getenv("EDITOR")
   if (editor ~= nil) then exe = editor end

   local tmp, tname = temp_file()
   if (tmp == nil) then return nil end
   basetext = "MT: " .. string.gsub(basetext, "\n", "\nMT: ") .. "\n"
   tmp:write(user_log_message)
   tmp:write(basetext)
   io.close(tmp)

   if (execute(exe, tname) ~= 0) then
      os.remove(tname)
      return nil
   end

   tmp = io.open(tname, "r")
   if (tmp == nil) then os.remove(tname); return nil end
   local res = ""
   local line = tmp:read()
   while(line ~= nil) do 
      if (not string.find(line, "^MT:")) then
         res = res .. line .. "\n"
      end
      line = tmp:read()
   end
   io.close(tmp)
   os.remove(tname)
   return res
end


function non_blocking_rng_ok()
   return true
end


function persist_phrase_ok()
   return true
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
   for test,res in pairs(old_results)
   do
      if res == true and new_results[test] ~= true
      then
         return false
      end
   end
   return true
end

-- merger support

function merge2_meld_cmd(lfile, rfile)
   return 
   function()
      return execute("meld", lfile, rfile)
   end
end

function merge3_meld_cmd(lfile, afile, rfile)
   return 
   function()
      return execute("meld", lfile, afile, rfile)
   end
end


function merge2_vim_cmd(vim, lfile, rfile, outfile)
   return
   function()
      return execute(vim, "-f", "-d", "-c", string.format("file %s", outfile),
                     lfile, rfile)
   end
end

function merge3_vim_cmd(vim, lfile, afile, rfile, outfile)
   return
   function()
      return execute(vim, "-f", "-d", "-c", string.format("file %s", outfile),
                     lfile, afile, rfile)
   end
end

function merge2_emacs_cmd(emacs, lfile, rfile, outfile)
   local elisp = "(ediff-merge-files \"%s\" \"%s\" nil \"%s\")"
   return 
   function()
      return execute(emacs, "-no-init-file", "-eval", 
                     string.format(elisp, lfile, rfile, outfile))
   end
end

function merge3_emacs_cmd(emacs, lfile, afile, rfile, outfile)
   local elisp = "(ediff-merge-files-with-ancestor \"%s\" \"%s\" \"%s\" nil \"%s\")"
   local cmd_fmt = "%s -no-init-file -eval " .. elisp
   return 
   function()
      execute(emacs, "-no-init-file", "-eval", 
              string.format(elisp, lfile, rfile, afile, outfile))
   end
end

function merge2_xxdiff_cmd(left_path, right_path, merged_path, lfile, rfile, outfile)
   return 
   function()
      return execute("xxdiff", 
                     "--title1", left_path,
                     "--title2", right_path,
                     lfile, rfile, 
                     "--merged-filename", outfile)
   end
end

function merge3_xxdiff_cmd(left_path, anc_path, right_path, merged_path, 
                           lfile, afile, rfile, outfile)
   return 
   function()
      return execute("xxdiff", 
                     "--title1", left_path,
                     "--title2", right_path,
                     "--title3", merged_path,
                     lfile, afile, rfile, 
                     "--merge", 
                     "--merged-filename", outfile)
   end
end
   
function merge2_kdiff3_cmd(left_path, right_path, merged_path, lfile, rfile, outfile)
   return 
   function()
      return execute("kdiff3", 
                     "--L1", left_path,
                     "--L2", right_path,
                     lfile, rfile, 
                     "-o", outfile)
   end
end

function merge3_kdiff3_cmd(left_path, anc_path, right_path, merged_path, 
                           lfile, afile, rfile, outfile)
   return 
   function()
      return execute("kdiff3", 
                     "--L1", anc_path,
                     "--L2", left_path,
                     "--L3", right_path,
                     afile, lfile, rfile, 
                     "--merge", 
                     "--o", outfile)
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

function get_preferred_merge2_command (tbl)
   local cmd = nil
   local left_path = tbl.left_path
   local right_path = tbl.right_path
   local merged_path = tbl.merged_path
   local lfile = tbl.lfile
   local rfile = tbl.rfile
   local outfile = tbl.outfile 

   local editor = string.lower(os.getenv("EDITOR"))

   
   if program_exists_in_path("kdiff3") then
      cmd =   merge2_kdiff3_cmd (left_path, right_path, merged_path, lfile, rfile, outfile) 
   elseif program_exists_in_path ("meld") then 
      tbl.meld_exists = true 
      io.write (string.format("\nWARNING: 'meld' was choosen to perform external 2-way merge.\n"..
          "You should merge all changes to *LEFT* file due to limitation of program\n"..
          "arguments.\n\n")) 
      cmd = merge2_meld_cmd (lfile, rfile) 
   elseif program_exists_in_path ("xxdiff") then 
      cmd = merge2_xxdiff_cmd (left_path, right_path, merged_path, lfile, rfile, outfile) 
   else
     if string.find(editor, "emacs") ~= nil or string.find(editor, "gnu") ~= nil then 
       if program_exists_in_path ("emacs") then 
          cmd = merge2_emacs_cmd ("emacs", lfile, rfile, outfile) 
       elseif program_exists_in_path ("xemacs") then 
          cmd = merge2_emacs_cmd ("xemacs", lfile, rfile, outfile) 
       end
     elseif string.find(editor, "vim") ~= nil then
       if os.getenv ("DISPLAY") ~= nil and program_exists_in_path ("gvim") then
          cmd = merge2_vim_cmd ("gvim", lfile, rfile, outfile) 
       elseif program_exists_in_path ("vim") then 
          cmd = merge2_vim_cmd ("vim", lfile, rfile, outfile) 
       end
     end
   end 
   return cmd 
end 

function merge2 (left_path, right_path, merged_path, left, right) 
   local ret = nil 
   local tbl = {}

   tbl.lfile = nil 
   tbl.rfile = nil 
   tbl.outfile = nil 
   tbl.meld_exists = false
 
   tbl.lfile = write_to_temporary_file (left) 
   tbl.rfile = write_to_temporary_file (right) 
   tbl.outfile = write_to_temporary_file ("") 

   if tbl.lfile ~= nil and tbl.rfile ~= nil and tbl.outfile ~= nil 
   then 
      tbl.left_path = left_path 
      tbl.right_path = right_path 
      tbl.merged_path = merged_path 

      local cmd = get_preferred_merge2_command (tbl) 

      if cmd ~=nil 
      then 
         io.write (string.format("executing external 2-way merge command\n"))
         cmd ()
         if tbl.meld_exists 
         then 
            ret = read_contents_of_file (tbl.lfile, "r")
         else
            ret = read_contents_of_file (tbl.outfile, "r") 
         end 
         if string.len (ret) == 0 
         then 
            ret = nil 
         end
      else
         io.write ("no external 2-way merge command found\n")
      end
   end

   os.remove (tbl.lfile)
   os.remove (tbl.rfile)
   os.remove (tbl.outfile)
   
   return ret
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

   local editor = string.lower(os.getenv("EDITOR"))

   if program_exists_in_path("kdiff3") then
      cmd = merge3_kdiff3_cmd (left_path, anc_path, right_path, merged_path, lfile, afile, rfile, outfile) 
   elseif program_exists_in_path ("meld") then 
      tbl.meld_exists = true 
      io.write (string.format("\nWARNING: 'meld' was choosen to perform external 3-way merge.\n"..
          "You should merge all changes to *CENTER* file due to limitation of program\n"..
          "arguments.\n\n")) 
      cmd = merge3_meld_cmd (lfile, afile, rfile) 
   elseif program_exists_in_path ("xxdiff") then 
      cmd = merge3_xxdiff_cmd (left_path, anc_path, right_path, merged_path, lfile, afile, rfile, outfile) 
   else 
     -- prefer emacs/xemacs
     if string.find(editor, "emacs") ~= nil or string.find(editor, "gnu") ~= nil then 
       if program_exists_in_path ("xemacs") then 
          cmd = merge3_emacs_cmd ("xemacs", lfile, afile, rfile, outfile) 
       elseif program_exists_in_path ("emacs") then 
          cmd = merge3_emacs_cmd ("emacs", lfile, afile, rfile, outfile) 
       end
     elseif string.find(editor, "vim") ~= nil then  -- prefer vim
       if os.getenv ("DISPLAY") ~= nil and program_exists_in_path ("gvim") then 
          cmd = merge3_vim_cmd ("gvim", lfile, afile, rfile, outfile) 
       elseif program_exists_in_path ("vim") then 
          cmd = merge3_vim_cmd ("vim", lfile, afile, rfile, outfile) 
       end
     end
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
         io.write (string.format("executing external 3-way merge command\n"))
         cmd ()
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
      else
         io.write ("no external 3-way merge command found\n")
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
   
	 -- today don't uses the time
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

function use_inodeprints()
   return false
end
