mtn_setup()

-- check the completion of long names and cancel names
check(mtn("version", "--fu"), 0, true, false)
check(qgrep("Changes since base revision", "stdout"))

-- if recognized correctly, --no-h should override the previously given --hidden
check(mtn("help", "--hidden", "--no-h"), 0, true, false)
check(not qgrep("benchmark_sha1", "stdout"))

-- ensure that once the option was identified, we print
-- out its full name for diagnostics
check(mtn("version", "--fu=arg"), 1, false, true)
check(qgrep("option 'full' does not take an argument", "stderr"))

check(mtn("help", "--no-h=arg"), 1, false, true)
check(qgrep("option 'no-hidden' does not take an argument", "stderr"))

check(mtn("version", "--f"), 1, false, true)
check(qgrep("option 'f' has multiple ambiguous expansions", "stderr"))

-- ensure that exact option name matches are not matched against
-- other option names which share the same prefix, like e.g. --key and --keydir
check(mtn("--key"), 1, false, true)
check(qgrep("missing argument to option 'key'", "stderr"))
check(not qgrep("option 'key' has multiple ambiguous expansions", "stderr"))

-- check that abbreviated options work - use --hidden / --no-hidden / --nh as an example
check(mtn("help"), 0, true, false)
rename("stdout", "no-hidden-expected")
check(mtn("help", "--hidden"), 0, true, false)
rename("stdout", "hidden-expected")
check(not samefile("no-hidden-expected", "hidden-expected"))
-- this is the important part
check(mtn("help", "--hidden", "--nh"), 0, true, false)
check(samefile("stdout", "no-hidden-expected"))

-- ensure that short options are not completed
check(mtn("push", "-b", "foo"), 1, false, true)
check(qgrep("option error: unknown option 'b'", "stderr"))
