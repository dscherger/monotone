--
-- Up until 0.40 mtn overwrote the database and keydir entries
-- in _MTN/options with the options specified by the user
-- without checking them further. This lead to workspace corruption
-- and the user had to edit _MTN/options by hand to let it point
-- to the valid entries again. This test ensures that at least
-- some basic file path / type checking is done on both, the
-- --database and --keydir options before they're actually written
-- to _MTN/options
--

-- setup a very simple workspace
mtn_setup()
check(mtn_ws_opts("add", "."), 0, false, false)

-- try to check the status and supply a non-existing database argument
check(mtn_ws_opts("status", "-d", "baz"), 1, false, true)
check(qgrep("baz does not exist", "stderr"))

-- this should succeed if the original database is still set
check(mtn_ws_opts("commit", "-m", "test"), 0, false, false)

--
-- now the keydir check, we do a minor change - to see the error,
-- comment out the previous commit
--
check(mtn_ws_opts("attr", "set", ".", "foo", "bar"), 0, false, false)

writefile("not_a_dir", "bla")
-- status needs to be able to get the key to display an Author: header but is
-- unable to do so with a bad key dir
check(mtn_ws_opts("status", "--keydir", "not_a_dir"), 1, false, false)

check(mtn_ws_opts("commit", "-m", "another test"), 0, false, false)

