local prefix_suggestor = clink.suggester("completion_prefix")

function prefix_suggestor:suggest(line_state, matches)
    -- If the input line is empty or only spaces, don't suggest anything.
    if not line_state:getline():match("[^ ]") then
        return
    end

    -- If there is no common prefix, don't suggest anything.
    local prefix = matches:getprefix()
    if prefix == "" then
        return
    end

    -- Return the common prefix as the suggestion.
    local info = line_state:getwordinfo(line_state:getwordcount())
    return prefix, info.offset
end
