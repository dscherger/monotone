function edit_comment(user_log_file)
  return string.gsub(user_log_file, "\nChangeLog: \n\n\n", "\nChangeLog: \n\nLog Entry\n")
end
