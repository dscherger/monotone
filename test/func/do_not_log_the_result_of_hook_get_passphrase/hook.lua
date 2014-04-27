function get_passphrase(keyid)
   if keyid.given_name == "quux" then return "xyzzypassphrasexyzzy" end
   return keyid.given_name
end
