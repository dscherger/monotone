function get_local_key_name(identity)
   if identity.id == "46ec58576f9e4f34a9eede521422aa5fd299dc50" then
      return "my_key" -- standard testsuite key
   elseif identity.id == "79f52f125f27f828cc4dd6831ee94f33876a2658" then
      return "their_key" -- loaded from bad_test_key
   else
      return "unknown"
   end
end
