-- Add a file with Windows newlines (CRLF) then commit.
-- An `mtn cat FILENAME > FILENAME` should result in having
-- exactly the same file.
-- On Windows XP cmd.exe (and probably other Windows systems),
-- the above results in the newlines being garbled and viewing the
-- file in some text editors has extra newlines added. It's not
-- specific to cmd.exe and '>' redirection either, anything that
-- reads from the mtn process stdout will see the text garbled.

mtn_setup()

addfile("numbers", "1\r\n2\r\n")
commit()

check(mtn("cat", "numbers"), 0, true, false)
writefile("expected", "1\r\n2\r\n")
check(samefile("stdout",  "expected"))

