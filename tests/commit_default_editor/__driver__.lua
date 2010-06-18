-- This tests the standard implementation of the edit_comment lua hook,
-- which should look for an "editor" executable on the PATH and run it
-- if neither $EDITOR nor $VISUAL is set in the environment.  We have
-- to override the default test hooks, which disable edit_comment.
--
-- Also test bad --date-format; doesn't fail, just uses the default

mtn_setup()
addfile("a", "hello there")

check(get("test_hooks.lua")) -- this restores the default edit_comment
                             -- and provides a fake "editor" executable

check(mtn("--branch", "testbranch", "commit", "--date-format", "foo"), 0, false, true)

if ostype ~= "Windows" then
   -- date parsing never works on Win32, so warning is suppressed
   check(qgrep("date format 'foo' cannot be parsed; using default instead", "stderr"))
end
