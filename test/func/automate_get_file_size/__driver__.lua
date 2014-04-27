
mtn_setup()

check(mtn("au", "get_file_size", string.rep("0123", 10)), 1, false, true)
check(qgrep("no file version " .. string.rep("0123", 10) .. " found in database", "stderr"))

addfile("foo", "")
commit()

local files = { "random1", "random2", "random3" }
local data = {}

-- build the data forward
for _,v in pairs(files) do
    get(v)
    contents = readfile(v)
    expected = string.len(contents)
    check(mtn("au", "identify", v), 0, true, false)
    ident = string.sub(readfile("stdout"), 1, -2)

    writefile("foo", contents)
    commit()

    data[v] = { ["expected"]=expected, ["ident"]=ident }
end

-- ...and check them in reverse
for idx = #files, 1, -1 do
    check(mtn("au", "get_file_size", data[files[idx]].ident), 0, true, false)
    got = string.sub(readfile("stdout"), 1, -2)
    check(data[files[idx]].expected == tonumber(got))
end

