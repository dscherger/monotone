function edit_comment(user_log_file)
   return string.gsub(user_log_file, "... REMOVE THIS LINE TO CANCEL THE COMMIT ...\n", "")
end
