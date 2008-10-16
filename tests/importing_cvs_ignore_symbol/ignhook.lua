function ignore_cvs_symbol(symbol_name)
    if symbol_name == "TAG_TO_IGNORE"
    then
        return true
    else
        return false
    end
end

