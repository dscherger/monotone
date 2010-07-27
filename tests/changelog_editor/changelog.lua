function edit_comment(user_log_file)

    -- commits that fail

    if (string.find(user_log_file, "\nempty message\n")) then
       return string.gsub(user_log_file, "\nempty message\n", "")
    elseif (string.find(user_log_file, "\nmissing instructions\n")) then
       return "foobar" .. user_log_file
    elseif (string.find(user_log_file, "\ncancel hint removed\n")) then
       return string.gsub(user_log_file, "... REMOVE THIS LINE TO CANCEL THE COMMIT ...\n", "")
    elseif (string.find(user_log_file, "\ncancel hint moved\n")) then
       return string.gsub(user_log_file, "(... REMOVE THIS LINE TO CANCEL THE COMMIT ...)", "\n\n%1")
    elseif (string.find(user_log_file, "\nmissing separator\n")) then
       return string.gsub(user_log_file, "---------------\n", "\n")
    elseif (string.find(user_log_file, "\nmissing revision\n")) then
       return string.gsub(user_log_file, "\nRevision:", "\nrevision:")
    elseif (string.find(user_log_file, "\nmissing parent\n")) then
       return string.gsub(user_log_file, "\nAuthor:", "foobar\nAuthor:")
    elseif (string.find(user_log_file, "\nmissing author\n")) then
       return string.gsub(user_log_file, "\nAuthor:", "\nAutor:")
    elseif (string.find(user_log_file, "\nempty author\n")) then
       return string.gsub(user_log_file, "\nAuthor: [^\n]*\n", "\nAuthor: \n")
    elseif (string.find(user_log_file, "\nmissing date\n")) then
       return string.gsub(user_log_file, "\nDate:", "\nDate")
    elseif (string.find(user_log_file, "\nempty date\n")) then
       return string.gsub(user_log_file, "\nDate: [^\n]*\n", "\nDate: \n")
    elseif (string.find(user_log_file, "\nmissing branch\n")) then
       return string.gsub(user_log_file, "\nBranch:", "\nranch:")
    elseif (string.find(user_log_file, "\nempty branch\n")) then
       return string.gsub(user_log_file, "\nBranch: [^\n]*\n", "\nBranch: \n")
    elseif (string.find(user_log_file, "\nmissing blank line\n")) then
       return string.gsub(user_log_file, "\n\nChangelog:", "\nChangelog:")
    elseif (string.find(user_log_file, "\nmissing changelog\n")) then
       return string.gsub(user_log_file, "\nChangelog:", "\n")
    elseif (string.find(user_log_file, "\nmissing summary\n")) then
       return string.gsub(user_log_file, "\nChanges against parent ", "\nChange foobar")
    elseif (string.find(user_log_file, "\nduplicated summary\n")) then
       return string.gsub(user_log_file, "(Changes against parent .*)", "%1%1")
    elseif (string.find(user_log_file, "\ntrailing text\n")) then
       return user_log_file .. "foobar"
    end

    -- commits that succeed

    if (string.find(user_log_file, "\nchange author/date/branch\n")) then
       result = user_log_file
       result = string.gsub(result, "\nDate:     [^\n]*\n", "\nDate:     2010-02-02T02:02:02\n")
       result = string.gsub(result, "\nAuthor:   bobo\n",   "\nAuthor:   baba\n")
       result = string.gsub(result, "\nBranch:   left\n",   "\nBranch:   right\n")
       return result
    elseif (string.find(user_log_file, "\nsleep\n")) then
       date = string.match(user_log_file, "\nDate:     ([^\n]*)")
       sleep(2)
       return string.gsub(user_log_file, "\nChangelog: \n\nsleep", "\nChangelog: \n\nOld: " .. date)
    elseif (string.find(user_log_file, "\nchange date\n")) then
       return string.gsub(user_log_file, "\nDate:     [^\n]*\n", "\nDate:     2010-01-01T01:01:01\n")
    elseif (string.find(user_log_file, "\nchangelog line\n")) then
       return string.gsub(user_log_file, "\nChangelog: \n\nchangelog line", "\nChangelog:message on changelog line")
    elseif (string.find(user_log_file, "\nfull changelog\n")) then
       return string.gsub(user_log_file, "\nChangelog: \n\nfull changelog\n\n", "\nChangelog:no\nspace\naround\nthis\nchangelog\n-----")
    end

    return user_log_file
end
