skip_if(not existsonpath("expect"))
check({"bash", "--version"}, 0, true)
-- hashes/dictionaries/associative arrays are new in version 4
skip_if(qgrep("bash[, ]*version 3", "stdout"))

function expect(test)
   if monotone_path == nil then
      monotone_path = os.getenv("mtn")
      if monotone_path == nil then
	 err("'mtn' environment variable not set")
      end
   end
   check(get(test..".exp"), 0, false, false)
   if existsonpath("expect") then
      check({"expect",
	     "-c", "set mtn \""..cmd_as_str(mtn()).."\"",
	     "-c", "set initial_dir \""..initial_dir.."\"",
	     "-c", "set srcdir \""..srcdir.."\"",
	     "-c", "source library.exp",
	     "-f", test..".exp"}, 0, true, false)
      check(grep("<<success>>", "stdout"), 0, false, false)
   else
      check(false)
   end
end

get("library.exp")

mtn_setup()

-- complete_mtn_-
expect("complete_mtn_-")

-- complete_propagate
addfile("prop-test", "foo")
commit("prop-br1")
addfile("prop-test2", "bar")
commit("prop-bra2")
check(mtn("update","-r","h:prop-br1"), 0, false, false)
writefile("prop-test", "zoot")
commit("prop-br1")

expect("complete_propagate")

-- complete_commit
addfile("commit-test1", "foo")
addfile("commit-test2", "bar")

expect("complete_commit")
