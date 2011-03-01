function get_revision_cert_trust(a, b, c)
   pcall(includedir, "/this/fnord/path/does/not/exists")
   return true
end
