-- Test that --confdir gets passed properly on file: sync

skip_if(ostype == "Windows") -- file: not supported on native Win32

mtn_setup()

copy("test.db", "test2.db")

addfile("testfile", "foo")
commit()

rcfile = io.open('test_hooks.lua', 'a')
if rcfile ~= nil then
   rcfile:write('\n')
   rcfile:write('x = io.open("checkfile", "a")\n')
   rcfile:write('x:write(get_confdir() .. "\\n")')
   rcfile:write('x:close()\n')
   rcfile:close()
end

test_uri="file://" .. url_encode_path(test.root .. "/test2.db") .. "?testbranch"
check(mtn("sync", test_uri), 0, true, false)

n = 0


testroot_unix = string.gsub(test.root, '\\', '/')

for line in io.lines("checkfile") do
   check(line == testroot_unix)
   n = n + 1
end
check(n == 2)
