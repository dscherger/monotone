function edit_comment(user_log_file)
    local result, date
    
    -- commits that fail

    if (string.find(user_log_file, "empty message\n")) then
       return string.gsub(user_log_file, "empty message\n", "")
    elseif (string.find(user_log_file, "cancel hint removed\n")) then
       return "changelog modified, " .. string.gsub(user_log_file, "... REMOVE THIS LINE TO CANCEL THE COMMIT ...\n", "")
    elseif (string.find(user_log_file, "missing instructions\n")) then
       return string.gsub(user_log_file, "Enter a description", "\n")
    elseif (string.find(user_log_file, "missing author\n")) then
       return string.gsub(user_log_file, "\nAuthor:", "\nAutor:")
    elseif (string.find(user_log_file, "empty author\n")) then
       return string.gsub(user_log_file, "\nAuthor: [^\n]*\n", "\nAuthor: \n")
    elseif (string.find(user_log_file, "missing date\n")) then
       return string.gsub(user_log_file, "\nDate:", "\nDate")
    elseif (string.find(user_log_file, "empty date\n")) then
       return string.gsub(user_log_file, "\nDate: [^\n]*\n", "\nDate: \n")
    elseif (string.find(user_log_file, "missing branch\n")) then
       return string.gsub(user_log_file, "\nBranch:", "\nranch:")
    elseif (string.find(user_log_file, "empty branch\n")) then
       return string.gsub(user_log_file, "\nBranch: [^\n]*\n", "\nBranch: \n")
    end

    -- commits that succeed

    if (string.find(user_log_file, "change author/date/branch\n")) then
       result = user_log_file
       result = string.gsub(result, "\nBranch:    left\n",   "\nBranch:   right\n")
       result = string.gsub(result, "\nAuthor:    bobo\n",   "\nAuthor:   baba\n")
       result = string.gsub(result, "\nDate: [^\n]*\n", "\nDate:     2010-02-02T02:02:02\n")
       io.stderr:write("result:\n" .. result)
       return result
    elseif (string.find(user_log_file, "sleep\n")) then
       date = string.match(user_log_file, "\nDate: ([^\n]*)")
       sleep(2)
       return string.gsub(user_log_file, "sleep", "Old: " .. date)
    elseif (string.find(user_log_file, "change date\n")) then
       return string.gsub(user_log_file, "\nDate: [^\n]*\n", "\nDate:     2010-01-01T01:01:01\n")
    elseif (string.find(user_log_file, "full changelog\n")) then
       return string.gsub(user_log_file, "full changelog\n\n", "full changelog\n-----")
    end

    -- otherwise don't modify anything
    return user_log_file
end
