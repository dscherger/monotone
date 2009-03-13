skip_if(not existsonpath("git"))

mtn_setup()

writefile("author.map", "tester@test.net = <tester@test.net>\n")

writefile("file1", "file1")
writefile("file2", "file2")
writefile("file3", "file3")

check(mtn("add", "file1", "file2", "file3"), 0, false, false)
commit()
r1 = base_revision()
check(mtn("tag", r1, "tag1"), 0, false, false)

check(mtn("mv", "file1", "tmp"), 0, false, false)
check(mtn("mv", "file3", "file1"), 0, false, false)
check(mtn("mv", "file2", "file3"), 0, false, false)
check(mtn("mv", "tmp", "file2"), 0, false, false)
commit()
r2 = base_revision()
check(mtn("tag", r2, "tag2"), 0, false, false)

-- export the monotone history and import it into git

mkdir("git.dir")
check(mtn("git_export", "--authors-file", "author.map"), 0, true, false)
copy("stdout", "stdin")
check(indir("git.dir", {"git", "init"}), 0, false, false)
check(indir("git.dir", {"git", "fast-import"}), 0, false, false, true)

-- check the tags we made on each rev above

check(mtn("co", "-r", "t:tag1", "mtn.dir"), 0, false, false)
check(indir("git.dir", {"git", "checkout", "tag1"}), 0, false, false)
check(samefile("mtn.dir/file1", "git.dir/file1"))
check(samefile("mtn.dir/file2", "git.dir/file2"))
check(samefile("mtn.dir/file3", "git.dir/file3"))

remove("mtn.dir")
check(mtn("co", "-r", "t:tag2", "mtn.dir"), 0, false, false)
check(indir("git.dir", {"git", "checkout", "tag2"}), 0, false, false)
check(samefile("mtn.dir/file1", "git.dir/file1"))
check(samefile("mtn.dir/file2", "git.dir/file2"))
check(samefile("mtn.dir/file3", "git.dir/file3"))

-- log both repos (mainly for visual inspection)

check(indir("mtn.dir", mtn("log")), 0, false, false)
check(indir("git.dir", {"git", "log", "-M", "--summary", "-p", "--pretty=raw"}), 0, false, false)
