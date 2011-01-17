mtn_setup()
addfile("a", "hello world")
commit()
writefile("a", "aaa")

check(get("changelog.lua"))

-- commits that fail


-- commit fails with empty message

writefile("_MTN/log", "empty message")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("empty log message", "stderr"))
check(not exists("_MTN/commit"))

-- commit can be cancelled, with modified changelog saved

writefile("_MTN/log", "cancel hint removed")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Commit cancelled.", "stderr"))
check(not exists("_MTN/commit"))
check(readfile("_MTN/log") == "changelog modified, cancel hint removed\n")

-- commit fails with modified/missing instructions

writefile("_MTN/log", "missing instructions")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Instructions not found", "stderr"))
check(exists("_MTN/commit"))
check(fsize("_MTN/commit") > 0)
id1=sha1("_MTN/commit")

-- commit fails if _MTN/commit exists from previously failed commit
check(mtn("commit"), 1, false, true)
check(qgrep("previously failed commit", "stderr"))
check(exists("_MTN/commit"))
check(fsize("_MTN/commit") > 0)
id2=sha1("_MTN/commit")
check(id1 == id2)
remove("_MTN/commit")

-- commit fails with modified/missing Author: line

writefile("_MTN/log", "missing author")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Author header not found", "stderr"))
check(exists("_MTN/commit"))
check(fsize("_MTN/commit") > 0)
remove("_MTN/commit")

-- commit fails with empty Author: line

writefile("_MTN/log", "empty author")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Author value empty", "stderr"))
check(exists("_MTN/commit"))
check(fsize("_MTN/commit") > 0)
remove("_MTN/commit")

-- commit fails with modified/missing Date: line

writefile("_MTN/log", "missing date")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Date header not found", "stderr"))
check(exists("_MTN/commit"))
check(fsize("_MTN/commit") > 0)
remove("_MTN/commit")

-- commit fails with empty Date: line

writefile("_MTN/log", "empty date")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Date value empty", "stderr"))
check(exists("_MTN/commit"))
check(fsize("_MTN/commit") > 0)
remove("_MTN/commit")

-- commit fails with modified/missing Branch: line

writefile("_MTN/log", "missing branch")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Branch header not found", "stderr"))
check(exists("_MTN/commit"))
check(fsize("_MTN/commit") > 0)
remove("_MTN/commit")

-- commit fails with empty Branch: line

writefile("_MTN/log", "empty branch")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Branch value empty", "stderr"))
check(exists("_MTN/commit"))
check(fsize("_MTN/commit") > 0)
remove("_MTN/commit")


-- commits that succeed

-- commit succeeds with bad date format (uses default format instead)

writefile("_MTN/log", "ok")
check(mtn("commit", "--date-format", "%Y-%m-%d", "--rcfile=changelog.lua"), 0, false, true)
if ostype == "Windows" then
   -- date parsing never works on Win32, so
   -- CMD_PRESET_OPTIONS(commit) specifies --no-format-dates, and
   -- we don't get a warning message.
else
   check(qgrep("warning: .* using default instead", "stderr"))
end


-- test unchanged --date, --author and --branch options

writefile("a", "a2.1")
writefile("_MTN/log", "fine")
check(mtn("commit", "--rcfile=changelog.lua", "--date", "2010-01-01T01:01:01", "--author", "bobo", "--branch", "left"), 0, false, false)
check(mtn("log", "--last", "1", "--no-graph"), 0, true, false)
check(qgrep("Date:     2010-01-01T01:01:01", "stdout"))
check(qgrep("Author:   bobo", "stdout"))
check(qgrep("Branch:   left", "stdout"))
check(not exists("_MTN/commit"))

-- test changed --date, --author and --branch options

writefile("a", "a2.2")
writefile("_MTN/log", "change author/date/branch")
check(mtn("commit", "--rcfile=changelog.lua", "--date", "2010-01-01T01:01:01", "--author", "bobo", "--branch", "left"), 0, false, false)
check(mtn("log", "--last", "1", "--no-graph"), 0, true, false)
check(not qgrep("Date:     2010-01-01T01:01:01", "stdout"))
check(not qgrep("Author:   bobo", "stdout"))
check(not qgrep("Branch:   left", "stdout"))
check(not exists("_MTN/commit"))

-- test unchanged date gets updated to reflect current time

writefile("a", "a3.1")
writefile("_MTN/log", "sleep")
check(mtn("commit", "--rcfile=changelog.lua"), 0, false, false)
check(mtn("log", "--last", "1", "--no-graph"), 0, true, false)
log = readfile("stdout")
old = string.match(log, "Old: ([^\n]*)")
new = string.match(log, "Date: ([^\n]*)")
check(old ~= new)
check(not exists("_MTN/commit"))

-- test changed date does not get updated

writefile("a", "a3.2")
writefile("_MTN/log", "change date")
check(mtn("commit", "--rcfile=changelog.lua"), 0, false, false)
check(mtn("log", "--last", "1", "--no-graph"), 0, true, false)
check(qgrep("Date:     2010-01-01T01:01:01", "stdout"))
check(not exists("_MTN/commit"))

-- message filling entire Changelog section (no leading/trailing blank lines)

writefile("a", "a5")
writefile("_MTN/log", "full changelog")
check(mtn("commit", "--rcfile=changelog.lua"), 0, false, false)
check(not exists("_MTN/commit"))
