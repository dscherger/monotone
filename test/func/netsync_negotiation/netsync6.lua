include("test_hooks.lua")
function get_passphrase(key)
   return key
end
function get_revision_cert_trust()
   return true
end