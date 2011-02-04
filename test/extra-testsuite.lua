-- Copyright (C) 2011 Richard Levitte <richard@levitte.org>
--
-- This program is made available under the GNU GPL version 2.0 or
-- greater. See the accompanying file COPYING for details.
--
-- This program is distributed WITHOUT ANY WARRANTY; without even the
-- implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
-- PURPOSE.

testdir = srcdir.."/extra-tests"

function prepare_to_run_tests (P)
   monotone_path = getpathof("mtn")
   if monotone_path == nil then monotone_path = "mtn" end
   set_env("mtn", monotone_path)

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
