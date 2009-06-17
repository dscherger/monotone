function get_passphrase(keyid)
   if keyid == "quux" then return "xyzzypassphrasexyzzy" end
   return keyid.given_name
end
