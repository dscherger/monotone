-- Everything alice signs is trusted, nothing mallory signs is
-- trusted.  For certs signed by other people, everything is
-- trusted except for one particular cert...
function get_revision_cert_trust(signers, id, name, val)
   for k, v in pairs(signers) do
      if v.given_name == "alice@trusted.com" then return true end
      if v.given_name == "mallory@evil.com" then return false end
   end
   -- the id of the revision in which "badfile" is checked in
   if (id == "4c71646d1def3c60b06d8358b1b7016d762e5e02"
       and name == "bad-cert" and val == "bad-val")
   then return false end
   return true
end

function get_manifest_cert_trust(signers, id, name, val)
   return true
end

function get_file_cert_trust(signers, id, name, val)
   return true
end

