-- Test automate key management functions:
-- generate_key (also tested in ../automate_genkey)
-- get_public_key
-- drop_public_key
-- read_packets (put_public_key)
-- keys

mtn_setup()

include("common/basic_io.lua")

check(mtn("automate", "generate_key", "foo@bar.com", "foopass"), 0, false, false)
check(mtn("pubkey", "foo@bar.com"), 0, true)
-- non-automate output uses OS-specific line endings, while automate uses Unix line endings.
canonicalize("stdout")
rename("stdout", "key_packet")
check(mtn("automate", "get_public_key", "foo@bar.com"), 0, true)
check(samefile("stdout", "key_packet"))
check(mtn("automate", "drop_public_key", "foo@bar.com"), 0, false, false)

-- drop_public_key does not drop private key
check(mtn("automate", "keys"), 0, true)
parsed = parse_basic_io(readfile("stdout"))
i = find_basic_io_line (parsed, {name = "local_name", values = "foo@bar.com"})
i = i + 1
check_basic_io_line (i, parsed[i], "public_location", {"keystore"}, false)
i = i + 1
check_basic_io_line (i, parsed[i], "private_location", {"keystore"}, false)

check(mtn("automate", "read_packets", readfile("key_packet")), 0)
check(mtn("automate", "keys"), 0, true)
parsed = parse_basic_io(readfile("stdout"))
i = find_basic_io_line (parsed, {name = "local_name", values = "foo@bar.com"})
i = i + 1
check_basic_io_line (i, parsed[i], "public_location", {"database", "keystore"}, false)
i = i + 1
check_basic_io_line (i, parsed[i], "private_location", {"keystore"}, false)
