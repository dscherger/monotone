function edit_comment(user_log_file)
  return string.gsub(user_log_file, "\nChangelog: \n\n\n", "\nChangelog: \n\nfoobar\n")
end
