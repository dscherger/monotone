function get_revision_cert_trust(signers, id, name, val)
    if (name == "date") then
        return false
    end
    return true
end

