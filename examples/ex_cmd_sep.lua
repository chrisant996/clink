-- In this example, a custom classifier applies colors to command separators and
-- redirection symbols in the input line.
local cmdsep_classifier = clink.classifier(50)
function cmdsep_classifier:classify(commands)
    -- The `classifications:classifyword()` method can only affect the words for
    -- the corresponding command.  However, this example doesn't need to parse
    -- words within commands, it just wants to parse the whole line.  And since
    -- each command's `classifications:applycolor()` method can apply color
    -- anywhere in the entire input line, this example can simply use the first
    -- command's `classifications` object.
    if commands[1] then
        local line_state = commands[1].line_state
        local classifications = commands[1].classifications
        local line = line_state:getline()
        local quote = false
        local i = 1
        while (i <= #line) do
            local c = line:sub(i,i)
            if c == '^' then
                i = i + 1
            elseif c == '"' then
                quote = not quote
            elseif quote then
            elseif c == '&' or c == '|' then
                classifications:applycolor(i, 1, "95")
            elseif c == '>' or c == '<' then
                classifications:applycolor(i, 1, "35")
                if line:sub(i,i+1) == '>&' then
                    i = i + 1
                    classifications:applycolor(i, 1, "35")
                end
            end
            i = i + 1
        end
    end
end
