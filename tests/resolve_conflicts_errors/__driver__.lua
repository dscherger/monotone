-- Test that typical user errors with 'conflicts' give good error messages.

mtn_setup()

----------
-- Conflict that is not supported; attribute
addfile("simple_file", "simple\none\ntwo\nthree\n")
commit("testbranch", "base")
base = base_revision()

check(mtn("attr", "set", "simple_file", "foo", "1"), 0, nil, nil)
commit("testbranch", "left 1")
left_1 = base_revision()

revert_to(base)

check(mtn("attr", "set", "simple_file", "foo", "2"), 0, nil, nil)
commit("testbranch", "right 1")
right_1 = base_revision()

-- invalid number of parameters for 'store'
check(mtn("conflicts", "store", left_1), 1, nil, true)
check(mtn("conflicts", "store", left_1, right_1, right_1), 1, nil, true)

check(mtn("conflicts", "store", left_1, right_1), 0, nil, true)
canonicalize("stderr")
check(samefilestd("conflicts-attr-store-1", "stderr"))

check(mtn("conflicts", "show_remaining", "foo"), 1, nil, true)
check(qgrep("wrong number of arguments", "stderr"))

check(mtn("conflicts", "show_remaining"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("conflicts-attr-show-1", "stderr"))

check(mtn("conflicts", "show_first", "foo"), 1, nil, true)
check(qgrep("wrong number of arguments", "stderr"))

check(mtn("conflicts", "show_first"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("conflicts-attr-show-2", "stderr"))

----------
-- specify conflicts file not in bookkeeping dir
check(mtn("conflicts", "--conflicts-file", "conflicts", "store", left_1, right_1), 1, nil, true)
canonicalize("stderr")
check(grep("-v", "detected at", "stderr"), 0, true)
check(samefilestd("conflicts-attr-store-2", "stdout"))

----------
-- use old conflicts file for new merge

-- get rid of attr conflict, add file content conflict
check(mtn("attr", "set", "simple_file", "foo", "1"), 0, nil, nil)
writefile("simple_file", "simple\ntwo\nthree\nfour\n")
commit("testbranch", "right 2")

-- attempt merge with old conflict file
check(mtn("merge", "--resolve-conflicts"), 1, nil, true)
canonicalize("stderr")
check(grep("-v", "detected at", "stderr"), 0, true)
check(samefilestd("merge-old-conflicts-file", "stdout"))


----------
-- specify inconsistent left and right resolutions for duplicate_name

addfile("checkout.sh", "checkout.sh right 1")
commit("testbranch", "right 3")

revert_to(left_1)
addfile("checkout.sh", "checkout.sh left 1")
commit("testbranch", "left 2")

check(mtn("conflicts", "store"), 0, true, nil)

-- invalid number of params
check(mtn("conflicts", "resolve_first_left", "user"), 1, nil, true)
check(qgrep("wrong number of arguments", "stderr"))
check(mtn("conflicts", "resolve_first_left", "user", "checkout.sh", "foo"), 1, nil, true)
check(qgrep("wrong number of arguments", "stderr"))
check(mtn("conflicts", "resolve_first_right", "user"), 1, nil, true)
check(qgrep("wrong number of arguments", "stderr"))
check(mtn("conflicts", "resolve_first_right", "user", "checkout.sh", "foo"), 1, nil, true)
check(qgrep("wrong number of arguments", "stderr"))

-- both sides specify user file
check(mtn("conflicts", "resolve_first_left", "user", "checkout.sh"), 0, nil, nil)
check(mtn("conflicts", "resolve_first_right", "user", "checkout.sh"), 1, nil, true)
canonicalize("stderr")
check(grep("-v", "detected at", "stderr"), 0, true)
check("mtn: misuse: left and right resolutions cannot both be 'user'\n" == readfile("stdout"))

-- end of file
