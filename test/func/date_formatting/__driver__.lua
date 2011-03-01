mtn_setup()
addfile("testfile", "floooooo")
check(mtn("commit", "--date=1999-12-31T13:00:00",
	  "--branch=B", "--message=blah-blah"), 0, false, false)
rev = base_revision()

function logdf(fmt)
   if fmt then
      return mtn("log", "--brief", "--no-graph", "--date-format="..fmt)
   else
      return mtn("log", "--brief", "--no-graph")
   end
end

function test_tz_fmt(tz, fmt, exp)
   if tz then
      set_env("TZ", tz)
   end
   check(logdf(fmt), 0, true, false)
   check(samelines("stdout", { rev .. " tester@test.net " .. exp .. " B" }))
end

test_tz_fmt(nil, nil,                 "1999-12-31T13:00:00")
test_tz_fmt(nil, "%Y-%m-%d %H:%M:%S", "1999-12-31 13:00:00")
test_tz_fmt(nil, "%Y-%m-%d %I:%M:%S", "1999-12-31 01:00:00")

-- check that --date-format=xxx and --no-format-dates override eachother
check(mtn("log", "--brief", "--no-graph", "--date-format=%Y", "--no-format-dates"),
      0, true, false)
check(samelines("stdout", { rev .. " tester@test.net 1999-12-31T13:00:00 B" }))
check(mtn("log", "--brief", "--no-graph", "--no-format-dates", "--date-format=%Y"),
      0, true, false)
check(samelines("stdout", { rev .. " tester@test.net 1999 B" }))


-- Windows' strftime() doesn't support %T, and MinGW uses the
-- version provided by Windows rather than having its own.
skip_if(ostype=="Windows")
test_tz_fmt(nil, "%a, %d %b %Y %T",   "Fri, 31 Dec 1999 13:00:00")

-- TZ env var doesn't work
--skip_if(ostype=="Windows") -- but we'll have already skipped

test_tz_fmt("ZZT+00:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 13:00 ZZT+0000")
test_tz_fmt("ZZT+01:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 12:00 ZZT-0100")
test_tz_fmt("ZZT+02:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 11:00 ZZT-0200")
test_tz_fmt("ZZT+03:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 10:00 ZZT-0300")
test_tz_fmt("ZZT+04:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 09:00 ZZT-0400")
test_tz_fmt("ZZT+05:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 08:00 ZZT-0500")
test_tz_fmt("ZZT+06:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 07:00 ZZT-0600")
test_tz_fmt("ZZT+07:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 06:00 ZZT-0700")
test_tz_fmt("ZZT+08:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 05:00 ZZT-0800")
test_tz_fmt("ZZT+09:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 04:00 ZZT-0900")
test_tz_fmt("ZZT+10:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 03:00 ZZT-1000")
test_tz_fmt("ZZT+11:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 02:00 ZZT-1100")
test_tz_fmt("ZZT+12:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 01:00 ZZT-1200")

test_tz_fmt("ZZT-00:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 13:00 ZZT+0000")
test_tz_fmt("ZZT-01:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 14:00 ZZT+0100")
test_tz_fmt("ZZT-02:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 15:00 ZZT+0200")
test_tz_fmt("ZZT-03:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 16:00 ZZT+0300")
test_tz_fmt("ZZT-04:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 17:00 ZZT+0400")
test_tz_fmt("ZZT-05:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 18:00 ZZT+0500")
test_tz_fmt("ZZT-06:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 19:00 ZZT+0600")
test_tz_fmt("ZZT-07:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 20:00 ZZT+0700")
test_tz_fmt("ZZT-08:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 21:00 ZZT+0800")
test_tz_fmt("ZZT-09:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 22:00 ZZT+0900")
test_tz_fmt("ZZT-10:00", "%Y-%m-%d %H:%M %Z%z", "1999-12-31 23:00 ZZT+1000")
test_tz_fmt("ZZT-11:00", "%Y-%m-%d %H:%M %Z%z", "2000-01-01 00:00 ZZT+1100")
test_tz_fmt("ZZT-12:00", "%Y-%m-%d %H:%M %Z%z", "2000-01-01 01:00 ZZT+1200")
