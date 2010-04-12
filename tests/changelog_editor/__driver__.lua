mtn_setup()
addfile("a", "hello world")
commit()
writefile("a", "aaa")

check(get("changelog.lua"))

-- status warns with bad date format

check(mtn("status"), 0, false, false)
check(mtn("status", "--date-format", "%F"), 0, false, true)
check(qgrep("date format", "stderr"))


-- commits that fail


-- commit fails with bad date format

check(mtn("commit", "--date-format", "%F"), 1, false, true)
check(qgrep("date format", "stderr"))

-- commit fails with empty message

writefile("_MTN/log", "empty message")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("empty log message", "stderr"))

-- commit fails with modified/missing instructions

writefile("_MTN/log", "missing instructions")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Instructions not found", "stderr"))

-- commit can be cancelled

writefile("_MTN/log", "cancel")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Commit cancelled.", "stderr"))

-- commit fails with modified/missing separator, Revision: or Parent: lines

writefile("_MTN/log", "missing separator")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Revision/Parent header not found", "stderr"))

writefile("_MTN/log", "missing revision")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Revision/Parent header not found", "stderr"))

writefile("_MTN/log", "missing parent")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Revision/Parent header not found", "stderr"))

-- commit fails with modified/missing Author: line

writefile("_MTN/log", "missing author")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Author header not found", "stderr"))

-- commit fails with empty Author: line

writefile("_MTN/log", "empty author")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Author value empty", "stderr"))

-- commit fails with modified/missing Date: line

writefile("_MTN/log", "missing date")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Date header not found", "stderr"))

-- commit fails with empty Date: line

writefile("_MTN/log", "empty date")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Date value empty", "stderr"))

-- commit fails with modified/missing Branch: line

writefile("_MTN/log", "missing branch")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Branch header not found", "stderr"))

-- commit fails with empty Branch: line

writefile("_MTN/log", "empty branch")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Branch value empty", "stderr"))

-- commit fails with modified/missing blank line before ChangeLog section

writefile("_MTN/log", "missing blank line")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("ChangeLog header not found", "stderr"))

-- commit fails with modified/missing ChangeLog section

writefile("_MTN/log", "missing changelog")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("ChangeLog header not found", "stderr"))

-- commit fails with missing ChangeSet summary section

writefile("_MTN/log", "missing summary")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("ChangeSet summary not found", "stderr"))

-- commit fails with duplicated ChangeSet: section

writefile("_MTN/log", "duplicated summary")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Text following ChangeSet summary", "stderr"))

-- commit fails with new text after Changes: section

writefile("_MTN/log", "trailing text")
check(mtn("commit", "--rcfile=changelog.lua"), 1, false, true)
check(qgrep("Text following ChangeSet summary", "stderr"))


-- commits that succeed


-- test unchanged --date, --author and --branch options

writefile("a", "a2.1")
writefile("_MTN/log", "fine")
check(mtn("commit", "--rcfile=changelog.lua", "--date", "2010-01-01T01:01:01", "--author", "bobo", "--branch", "left"), 0, false, false)
check(mtn("log", "--last", "1", "--no-graph"), 0, true, false)
check(qgrep("Date:     2010-01-01T01:01:01", "stdout"))
check(qgrep("Author:   bobo", "stdout"))
check(qgrep("Branch:   left", "stdout"))

-- test changed --date, --author and --branch options 

writefile("a", "a2.2")
writefile("_MTN/log", "change author/date/branch")
check(mtn("commit", "--rcfile=changelog.lua", "--date", "2010-01-01T01:01:01", "--author", "bobo", "--branch", "left"), 0, false, false)
check(mtn("log", "--last", "1", "--no-graph"), 0, true, false)
check(not qgrep("Date:     2010-01-01T01:01:01", "stdout"))
check(not qgrep("Author:   bobo", "stdout"))
check(not qgrep("Branch:   left", "stdout"))

-- test unchanged date gets updated to reflect current time

writefile("a", "a3.1")
writefile("_MTN/log", "sleep")
check(mtn("commit", "--rcfile=changelog.lua"), 0, false, false)
check(mtn("log", "--last", "1", "--no-graph"), 0, true, false)
log = readfile("stdout")
old = string.match(log, "Old: ([^\n]*)")
new = string.match(log, "Date: ([^\n]*)")
check(old ~= new)

-- test changed date does not get updated

writefile("a", "a3.2")
writefile("_MTN/log", "change date")
check(mtn("commit", "--rcfile=changelog.lua"), 0, false, false)
check(mtn("log", "--last", "1", "--no-graph"), 0, true, false)
check(qgrep("Date:     2010-01-01T01:01:01", "stdout"))

-- message on same line as ChangeLog: header

writefile("a", "a4")
writefile("_MTN/log", "changelog line")
check(mtn("commit", "--rcfile=changelog.lua"), 0, false, false)

-- message filling entire ChangeLog section (no leading/trailing blank lines)

writefile("a", "a5")
writefile("_MTN/log", "full changelog")
check(mtn("commit", "--rcfile=changelog.lua"), 0, false, false)
