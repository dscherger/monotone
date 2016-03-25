-- Test 'mtn up' in the face of a special file

skip_if(ostype == "Windows") -- not sure what special files exist on
                             -- windows, nor how to create them.

mtn_setup()
check(get(".mtn-ignore"))

mkdir("dir")
addfile("dir/1", "file 1 within directory")
addfile("dir/2", "file 2 within directory")
addfile("file", "regular file")
check(mtn("add", ".mtn-ignore"), 0, false, false)
commit()
REV1=base_revision()

-- add another commit we can update to later on
addfile("dir/3", "file 3 within directory")
commit()

check(mtn("update", "-r", REV1), 0, false, false)

-- create two special files, fifo1 already being in the ignore list,
-- fifo2 remaining unknown.
check({"mkfifo", "fifo1"})
check({"mkfifo", "fifo2"})

-- then try to update to the latest revision, again
check(raw_mtn("update"), 0, true, true)

-- go back, create a conflicting special file on dir/3
check(mtn("update", "-r", REV1), 0, false, false)
check({"mkfifo", "dir/3"})

-- trying to update will fail due to the fifo on dir/3
check(raw_mtn("update"), 1, true, true)
check(qgrep("blocked by unversioned path", "stderr"))

-- allow monotone to move the fifo
check(raw_mtn("update", "--move-conflicting-paths"), 0, true, true)
check(qgrep("moved conflicting special file", "stderr"))
