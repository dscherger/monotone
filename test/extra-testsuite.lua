-- Copyright (C) 2011 Richard Levitte <richard@levitte.org>
--
-- This program is made available under the GNU GPL version 2.0 or
-- greater. See the accompanying file COPYING for details.
--
-- This program is distributed WITHOUT ANY WARRANTY; without even the
-- implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
-- PURPOSE.

testdir = srcdir.."/extra"

function prepare_to_run_tests (P)
   -- We do a dirty trick and include the functional testsuite, because
   -- we want all the functionality available for the those tests.
   include("../func-testsuite.lua")

   -- Because it just got redefined, we need to reset testdir and to run
   -- the redefined prepare_to_run_tests.
   testdir = srcdir.."/extra"
   return prepare_to_run_tests(P)
end
