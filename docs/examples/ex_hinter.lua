local hinter = clink.hinter()

function hinter:gethint(line_state)
    local h, p
    local cursorpos = line_state:getcursor()
    for i = 1, line_state:getwordcount() do
        local info = line_state:getwordinfo(i)
        if info.offset <= cursorpos and cursorpos <= info.offset + info.length then
            h = string.format("word starts at %d", info.offset)
            p = info.offset
        end
    end
    return h, p
end
