-- Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
--
-- This program is made available under the GNU GPL version 2.0 or
-- greater. See the accompanying file COPYING for details.
--
-- This program is distributed WITHOUT ANY WARRANTY; without even the
-- implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
-- PURPOSE.

-- This test suite is special; it synthesizes all its __driver__.lua
-- files on the fly.  Each one runs the 'unit_tester' binary over just
-- one of the test cases it can run.

testdir = srcdir.."/unit/tests"

function prepare_to_enumerate_tests (P)
   local unit_test_path = getpathof("test/bin/unit_tester")
   if unit_test_path == nil then return 1 end

   writefile_q("in", nil)
   prepare_redirect("in", "out", "err")
   local status = execute(unit_test_path)
   local out = readfile_q("out")
   local err = readfile_q("err")

   if status == 0 and err == "" and out ~= "" then
      -- clean up any on-the-fly tests from a previous run, in case the
      -- set changed; we mustn't blow away the entire directory because
      -- it's shared with the makefile (and possibly the source code)
      unlogged_mkdir(testdir)
      for _,candidate in ipairs(read_directory(testdir)) do
	 if exists(testdir .. "/" .. candidate .. "/__driver__.lua") then
	    unlogged_remove(testdir .. "/" .. candidate)
	 end
      end

      for tcase in string.gmatch(out, "[%w_:]+") do
	 local tdir = string.gsub(tcase, ':', '_')
	 unlogged_mkdir(testdir .. "/" .. tdir)
	 writefile_q(testdir .. "/" .. tdir .. "/__driver__.lua",
		     string.format("check({ %q, %q }, 0, true, true)\n",
				   unit_test_path, tcase))
      end
   else
      P(string.format("%s: exit %d\nstdout:\n", unit_test_path, status))
      P(out)
      P("stderr:\n")
      P(err)

      if status == 0 then status = 1 end
   end

   unlogged_remove("in")
   unlogged_remove("out")
   unlogged_remove("err")
   return status
end

-- Cloned from testsuite.lua; just dumps information about the monotone
-- build into the master logfile.

function prepare_to_run_tests (P)
   local monotone_path = getpathof("mtn")
   if monotone_path == nil then monotone_path = "mtn" end

   writefile_q("in", nil)
   prepare_redirect("in", "out", "err")

   local status = execute(monotone_path, "version", "--full")
   local out = readfile_q("out")
   local err = readfile_q("err")

   if status == 0 and err == "" and out ~= "" then
      logfile:write(out)
   else
      P(string.format("mtn version --full: exit %d\nstdout:\n", status))
      P(out)
      P("stderr:\n")
      P(err)

      if status == 0 then status = 1 end
   end

   unlogged_remove("in")
   unlogged_remove("out")
   unlogged_remove("err")
   return status
end
