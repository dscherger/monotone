-- this is equal to hooks.lua, except that we don't write to stdout
-- which would confuse the stdio parser
function expand_selector(...)
    print("this is catched")
    io.write("this is also catched")
    print("line breaks\nare handled\nproperly as well")

    io.stderr:write("this is also not catched\n")

    return nil
end
