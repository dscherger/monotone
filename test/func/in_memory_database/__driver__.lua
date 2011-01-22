include("common/netsync.lua")
mtn_setup()
netsync.setup()

addfile("foo", "bar")
commit()


srv = netsync.start({"-d", ":memory:"})

srv:push("*", 1)
srv:pull("*", 2)

check_same_db_contents("test.db", "test2.db")
