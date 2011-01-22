include("common/netsync.lua")
mtn_setup()
netsync.setup()

the_hook = "function note_netsync_end() server_set_listening(false) io.write('x') io.flush() end"
writefile("my_hooks.lua", the_hook)

srv = netsync.start({"--rcfile=my_hooks.lua"})
srv:pull("*")

check(srv:wait(1))
