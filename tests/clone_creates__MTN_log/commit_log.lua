function edit_comment(user_log_file)
  return string.gsub(user_log_file, "Changelog:\n\n\n", "Changelog:\n\nLog Entry\n")
end
