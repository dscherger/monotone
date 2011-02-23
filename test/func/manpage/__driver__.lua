mtn_setup()

check(mtn("manpage"), 0, true, false)
rename("stdout", "manpage")

-- check for a proper header line
check(mtn("version"), 0, true, false)
local s,e,version = string.find(readfile("stdout"), "(monotone %d+\.%d+%S*)")
check(qgrep(".TH \"monotone\" 1 \"[0-9]{4}-[0-9]{2}-[0-9]{2}\" \"" .. version .. "\"", "manpage"))

-- check required sections
check(qgrep(".SH \"NAME\"", "manpage"))
check(qgrep(".SH \"SYNOPSIS\"", "manpage"))

-- check the optional sections
check(qgrep(".SH \"DESCRIPTION\"", "manpage"))
check(qgrep(".SH \"GLOBAL OPTIONS\"", "manpage"))
check(qgrep(".SH \"COMMANDS\"", "manpage"))
check(qgrep(".SH \"SEE ALSO\"", "manpage"))
check(qgrep(".SH \"BUGS\"", "manpage"))
check(qgrep(".SH \"AUTHORS\"", "manpage"))
check(qgrep(".SH \"COPYRIGHT\"", "manpage"))
