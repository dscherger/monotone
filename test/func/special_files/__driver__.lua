-- Test various cases with special files

skip_if(ostype == "Windows") -- not sure what special files exist on
                             -- windows, nor how to create them.

mtn_setup()
check(get(".mtn-ignore"))
check(get("expected-inventory.stdout"))

mkdir("dir")
addfile("dir/1", "file 1 within directory")
addfile("dir/2", "file 2 within directory")
addfile("file", "regular file")
check(mtn("add", ".mtn-ignore"), 0, false, false)
commit()

-- create two special files
check({"mkfifo", "fifo1"})
check({"mkfifo", "fifo2"})

-- check if those get listed by 'ls unknown'
check(raw_mtn("ls", "unknown"), 0, true, false)
check(samelines("stdout", {
  "fifo1",
  "fifo2"
}))

-- add fifo1 to .mtn-ignore
append(".mtn-ignore", "^fifo1$\n")

-- check if fifo1 is properly ignored
check(raw_mtn("ls", "ignored"), 0, false, false)
check(qgrep("fifo1", "stdout"))

-- check 'ls unknown' again
check(raw_mtn("ls", "unknown"), 0, true, false)
check(samelines("stdout", {
  "fifo2",
}))

-- try adding, which should should fail
check(mtn("add", "fifo2"), 1, false, true)
check(qgrep("fifo2", "stderr"))
check(qgrep("special file", "stderr"))

-- check if automate inventory still works and lists both, the ignored
-- and the unknown fifo.
check(mtn("automate", "inventory"), 0, true, false)
check(qgrep("fifo1", "stdout"))
check(qgrep("fifo2", "stdout"))
canonicalize("stdout")
check(readfile("expected-inventory.stdout") == readfile("stdout"))

