-- Test automate key management functions:
-- genkey
-- pubkey
-- dropkey
-- read_packets (putkey)
-- keys

mtn_setup()

check(mtn("automate", "genkey", "foo@bar.com", "foopass"), 0, false, false)
check(mtn("pubkey", "foo@bar.com"), 0, true)
rename("stdout", "key_packet")
check(mtn("automate", "pubkey", "foo@bar.com"), 0, true)
check(samefile("stdout", "key_packet"))
check(mtn("automate", "dropkey", "foo@bar.com"), 0, false, false)

check(mtn("automate", "keys"), 0, true)
check(not qgrep("foo@bar.com", "stdout"))

check(mtn("automate", "read_packets", readfile("key_packet")), 0)
check(mtn("automate", "keys"), 0, true)
check(qgrep("foo@bar.com", "stdout"))
