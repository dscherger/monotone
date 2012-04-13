mtn_setup()

addfile("test1", "not more than 33 bytes of content")
addfile("test2", "some 29 bytes of junk content")
commit()

check(mtn("au", "identify", "test1"), 0, true, false)
local fileid1 = string.sub(readfile("stdout"), 1, -2)
check(mtn("au", "identify", "test2"), 0, true, false)
local fileid2 = string.sub(readfile("stdout"), 1, -2)

-- db should be ok
check(mtn("db", "check"), 0, false, false)

-- change the file size
check(mtn("db", "execute", "update file_sizes set size='20' where id=x'" .. fileid1 .. "';"), 0, false, false)

-- check
check(mtn("db", "check"), 1, false, true)
check(qgrep('1 missing or invalid file sizes', 'stderr'))
check(qgrep(fileid1, 'stderr'))
check(qgrep('serious problems detected', 'stderr'))

-- drop the second
check(mtn("db", "execute", "delete from file_sizes where id=x'" .. fileid2 .. "';"), 0, false, false)

-- check again
check(mtn("db", "check"), 1, false, true)
check(qgrep('2 missing or invalid file sizes', 'stderr'))
check(qgrep(fileid1, 'stderr'))
check(qgrep(fileid2, 'stderr'))
check(qgrep('serious problems detected', 'stderr'))

-- insert / fix everything again
check(mtn("db", "execute", "insert or replace into file_sizes values(x'" .. fileid1 .. "','33');"), 0, false, false)
check(mtn("db", "execute", "insert into file_sizes values(x'" .. fileid2 .. "','29');"), 0, false, false)

-- ... and everything is back to normal
check(mtn("db", "check"), 0, false, false)

