skip_if(not existsonpath("git"))

mtn_setup()

writefile("author.map", "tester@test.net = other <other@test.net>\n")

writefile("file1", "file1")
writefile("file2", "file2")

check(mtn("add", "file1", "file2"), 0, false, false)
check(mtn("commit", "-m", "add file1 and file2", "-b", "branch1"), 0, false, false)
r1 = base_revision()
check(mtn("tag", r1, "tag1"), 0, false, false)

writefile("file1", "file1 has changed")
check(mtn("commit", "-m", "edit file1", "-b", "branch1"), 0, false, false)
r2 = base_revision()
check(mtn("tag", r2, "tag2"), 0, false, false)

check(mtn("update", "-r", r1), 0, false, false)

check(mtn("rm", "file2"), 0, false, false)
check(mtn("commit", "-m", "remove file2", "-b", "branch2"), 0, false, false)
r3 = base_revision()
check(mtn("tag", r3, "tag3"), 0, false, false)

check(mtn("mv", "file1", "file-one"), 0, false, false)
check(mtn("commit", "-m", "rename file1 to file-one", "-b", "branch2"), 0, false, false)
r4 = base_revision()
check(mtn("tag", r4, "tag4"), 0, false, false)

check(mtn("propagate", "branch2", "branch1"), 0, false, false)
check(mtn("update", "-r", "h:branch1"), 0, false, false)
r5 = base_revision()
check(mtn("tag", r5, "tag5"), 0, false, false)

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

remove("mtn.dir")
check(mtn("co", "-r", "t:tag2", "mtn.dir"), 0, false, false)
check(indir("git.dir", {"git", "checkout", "tag2"}), 0, false, false)
check(samefile("mtn.dir/file1", "git.dir/file1"))
check(samefile("mtn.dir/file2", "git.dir/file2"))

remove("mtn.dir")
check(mtn("co", "-r", "t:tag3", "mtn.dir"), 0, false, false)
check(indir("git.dir", {"git", "checkout", "tag3"}), 0, false, false)
check(samefile("mtn.dir/file1", "git.dir/file1"))
check(not exists("mtn.dir/file2"))
check(not exists("git.dir/file2"))

remove("mtn.dir")
check(mtn("co", "-r", "t:tag4", "mtn.dir"), 0, false, false)
check(indir("git.dir", {"git", "checkout", "tag4"}), 0, false, false)
check(samefile("mtn.dir/file-one", "git.dir/file-one"))
check(not exists("mtn.dir/file2"))
check(not exists("git.dir/file2"))

remove("mtn.dir")
check(mtn("co", "-r", "t:tag5", "mtn.dir"), 0, false, false)
check(indir("git.dir", {"git", "checkout", "tag5"}), 0, false, false)
check(samefile("mtn.dir/file-one", "git.dir/file-one"))
check(not exists("mtn.dir/file2"))
check(not exists("git.dir/file2"))

-- log both repos and check the author mapping

check(indir("mtn.dir", mtn("log")), 0, true, false)
check(qgrep("Author: tester@test.net", "stdout"))
check(indir("git.dir", {"git", "log", "--summary", "--pretty=raw"}), 0, true, false)
check(qgrep("author other <other@test.net>", "stdout"))
check(qgrep("committer other <other@test.net>", "stdout"))

-- check branch refs

remove("mtn.dir")
check(mtn("co", "-r", "h:branch2", "mtn.dir"), 0, false, false)
check(indir("git.dir", {"git", "checkout", "branch2"}), 0, false, false)
check(samefile("mtn.dir/file-one", "git.dir/file-one"))
check(not exists("mtn.dir/file2"))
check(not exists("git.dir/file2"))

remove("mtn.dir")
check(mtn("co", "-r", "h:branch1", "mtn.dir"), 0, false, false)
check(indir("git.dir", {"git", "checkout", "branch1"}), 0, false, false)
check(samefile("mtn.dir/file-one", "git.dir/file-one"))
check(not exists("mtn.dir/file2"))
check(not exists("git.dir/file2"))

