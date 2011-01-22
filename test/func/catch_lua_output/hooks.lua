function expand_selector(...)
    print("this is catched")
    io.write("this is also catched")
    print("line breaks\nare handled\nproperly as well")

    io.stdout:write("this is not catched\n")
    io.stderr:write("this is also not catched\n")

    return nil
end
