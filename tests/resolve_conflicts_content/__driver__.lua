-- Demonstrate content conflict resolutions
--
-- All files in 'files' directory, all commands invoked there, to show
-- that 'conflicts store' uses the right bookkeeping directory.

mtn_setup()

get("merge.lua")

mkdir("files")
addfile("files/foo", "foo")
addfile("files/bar", "bar\none\ntwo\nthree")
addfile("files/baz", "baz\naaa\nbbb\nccc")
addfile("files/inter1", "inter1\naaa\nbbb\nccc")
addfile("files/inter2", "inter2\naaa\nbbb\nccc")
commit("testbranch", "base")
base = base_revision()

writefile("files/foo", "foo\nfirst\nrevision")
writefile("files/bar", "bar\nzero\none\ntwo\nthree")
writefile("files/baz", "baz\nAAA\nbbb\nccc")
writefile("files/inter1", "inter1\nAAA\nbbb\nccc")
writefile("files/inter2", "inter2\nAAA\nbbb\nccc")
commit("testbranch", "first")
first = base_revision()

revert_to(base)

writefile("files/foo", "foo\nsecond\nrevision")
writefile("files/bar", "bar\none\ntwo\nthree\nfour")
writefile("files/baz", "baz\nAaa\nbbb\nCCC")
writefile("files/inter1", "inter1\nAaa\nbbb\nccc")
writefile("files/inter2", "inter2\nAaa\nbbb\nccc")
commit("testbranch", "second")
second = base_revision()

-- We specify 'first second' so the left/right don't change as when we
-- make small changes to the test (default order is alphabetical rev id).
check(indir("files", mtn("conflicts", "store", first, second)), 0, nil, true)
check(samelines("stderr",
{"mtn: 5 conflicts with supported resolutions.",
 "mtn: stored in '_MTN/conflicts'"}))
check(samefilestd("conflicts-1", "_MTN/conflicts"))

-- invalid resolution identifier
check(mtn("conflicts", "resolve_first", "foo"), 1, nil, true)
check(samelines("stderr", { "mtn: misuse: 'foo' is not a supported conflict resolution for file_content"}))

-- bar is the first conflict (alphabetical by file name); it is
-- 'resolved_internal'. The rest are not resolved internal.
--
-- For baz and foo, we specify one user file in _MTN, one out, to
-- ensure mtn handles both.
writefile("files/foo", "foo\nmerged\nrevision")
mkdir("_MTN/result")
writefile("_MTN/result/baz", "baz\nAaa\nBbb\nCcc")

check(indir("files", mtn("conflicts", "resolve_first", "user", "../_MTN/result/baz")), 0, nil, nil)
check(indir("files", mtn("conflicts", "resolve_first", "user", "foo")), 0, nil, nil)
check(samefilestd("conflicts-2", "_MTN/conflicts"))

-- For inter1, inter2, we use 'interactive', with the default and
-- user-supplied file names. merge.lua overrides the merge hook to
-- just return the left file name as the merge result.
check(indir("files", mtn("--rcfile=../merge.lua", "conflicts", "resolve_first", "interactive")), 0, nil, true)
check(qgrep("interactive merge result saved in '_MTN/resolutions/files/inter1'", "stderr"))

check(indir("files", mtn("--rcfile=../merge.lua", "conflicts", "resolve_first", "interactive", "../_MTN/resolutions/inter_merged")), 0, nil, true)
check(qgrep("interactive merge result saved in '_MTN/resolutions/inter_merged'", "stderr"))

check(samefilestd("conflicts-3", "_MTN/conflicts"))

-- we specified 'first second' on 'conflicts store', so we need it
-- here as well; the default order is different.
check(mtn("explicit_merge", "--resolve-conflicts", first, second, "testbranch"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("merge-1", "stderr"))

check(mtn("update"), 0, nil, true)
canonicalize("stderr")
check(samefilestd("update-1", "stderr"))

check(readfile("files/foo") == "foo\nmerged\nrevision")
check(readfile("files/bar") == "bar\nzero\none\ntwo\nthree\nfour")
check(readfile("files/baz") == "baz\nAaa\nBbb\nCcc")
check(readfile("files/inter1") == "files/inter1")
check(readfile("files/inter2") == "files/inter2")
-- end of file
