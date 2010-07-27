
mtn_setup()
check(get("hooks.lua"))

function trusted(rev, name, value, ...) -- ... is signers
  check(mtn("trusted", "--rcfile", "hooks.lua", rev, name, value, ...), 0, true, false)
  local t = qgrep(" trusted", "stdout")
  local u = qgrep(" untrusted", "stdout") or qgrep(" UNtrusted", "stdout")
  check(t ~= u)
  return t
end

-- create two arbitrary revisions
addfile("goodfile", "good")
commit()
good = base_revision()

addfile("badfile", "bad")
commit()
bad = base_revision()

check(mtn("automate", "genkey", "foo@bar.com", "foo@bar.com"), 0, false, false)
check(mtn("automate", "genkey", "alice@trusted.com", "alice@trusted.com"), 0, false, false)
check(mtn("automate", "genkey", "mallory@evil.com", "mallory@evil.com"), 0, false, false)

-- Idea here is to check a bunch of combinations, to make sure that
-- trust hooks get all information correctly
check(trusted(good, "foo", "bar", "foo@bar.com"))
check(trusted(good, "foo", "bar", "alice@trusted.com"))
check(not trusted(good, "foo", "bar", "mallory@evil.com"))
check(trusted(good, "bad-cert", "bad-val", "foo@bar.com"))
check(trusted(bad, "good-cert", "bad-val", "foo@bar.com"))
check(trusted(bad, "bad-cert", "good-val", "foo@bar.com"))
check(not trusted(bad, "bad-cert", "bad-val", "foo@bar.com"))
check(trusted(bad, "bad-cert", "bad-val", "alice@trusted.com"))

check(trusted(good, "foo", "bar", "foo@bar.com", "alice@trusted.com"))
check(trusted(good, "foo", "bar", "alice@trusted.com", "foo@bar.com"))
check(not trusted(good, "foo", "bar", "foo@bar.com", "mallory@evil.com"))
check(not trusted(good, "foo", "bar", "mallory@evil.com", "foo@bar.com"))
check(trusted(bad, "bad-cert", "bad-val", "foo@bar.com", "alice@trusted.com"))
check(trusted(bad, "bad-cert", "bad-val", "alice@trusted.com", "foo@bar.com"))
